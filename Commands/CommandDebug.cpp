#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define CONSOLE_MAX_INSTRUCTIONS_LENGTH 22
#define CONSOLE_MAX_ROW_LENGTH 64
#define CONSOLE_INSTRUCTIONS_COUNT 5

#include "CommandDebug.h"
#include "CommandManager.h"
#include "BricoExtManager.h"
#include "DebugLink.h"
#include "DebugExpression.h"
#include "MemoryDump.h"
#include "DebugProtocol.h"
#include "InputBuffer.h"
#include "MassStorageManager.h"
#include "PIOManager.h"
#include "ROMImageManager.h"
#include "ROMUploadManager.h"
#include "Tui.h"
#include "TraceRoutine.h"
#include <pico/stdlib.h>

#define COMMAND_NAME "Dbg"

#define COMMAND_DESC_PARAMETERS "[tui]"
#define COMMAND_DESC_TEXT "68000 debug mode (needs trace mode + TRACE vector in the 68k code; 'tui' = full screen). Keys: s step, o/F10 step over, c continue, g run to address, b break, m memory address (empty = off; hex, $hex, d0-d7, a0-a7, sp, pc, +/-hex), w 8/16-bit, PgUp/PgDn scroll, F3 code/mem, x exit"
static_assert(sizeof(COMMAND_DESC_TEXT) <= COMMAND_MAX_LENGTH_DESC_TEXT, "Command description too long");

static_assert(TRACE_ROUTINE_SIZE_BYTES <= BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES, "TRACE handler too big for the BIOS patch area");
static_assert(sizeof(gwTraceRoutine) == TRACE_ROUTINE_SIZE_BYTES, "Inconsistent TraceRoutine.h");

#define TRACE_ROUTINE_ADDRESS_68K (0xC00000u + BIOS_PATCH_TRACE_ROUTINE_OFFSET)

/// @brief Size in words of the v1 stop frame (only used to recognise an old handler)
#define DEBUG_V1_FRAME_WORDS 59

#define MESSAGE_UNRECOGNIZED_FRAME "Unrecognized frame received - TRACE handler from another version?"

/// @brief Classic view: number of lines (16 bytes each) dumped by 'm'
#define MEMORY_CLASSIC_LINES 8
#define MEMORY_BYTES_PER_LINE 16
#define MEMORY_ADDRESS_SPACE 0x1000000u     // 68000: 24 bits address bus

/// @brief Memory zone displayed by the debugger (shared by both views). The dump is read at each stop (full screen
/// view, panel visible) or on demand (classic view). It is aligned on 16 bytes.
struct MemoryState
{
    MemoryDump dump;
    bool hasAddress = false;
    uint32_t address = 0;
    bool words = true;      // 16-bit words (default) or bytes, kept across stops and views
};

/// @brief Align the address on a line, keep the zone inside the 24 bits address space
static void NormalizeMemoryAddress(MemoryState &mem, int lines)
{
    uint32_t bytes = (uint32_t)lines * MEMORY_BYTES_PER_LINE;
    uint32_t address = mem.address & (MEMORY_ADDRESS_SPACE - 1) & ~(uint32_t)(MEMORY_BYTES_PER_LINE - 1);
    if (bytes > 0 && address > MEMORY_ADDRESS_SPACE - bytes)
    {
        address = MEMORY_ADDRESS_SPACE - bytes;
    }
    mem.address = address;
}

/// @brief Read the displayed zone with one READ_MEM request. The $300000-$3FFFFF zone (I/O, side effects) and the
/// BricoExt ports are never forced: DebugLink refuses them and we only report it.
/// @param bShift true at a stop (the previous dump becomes the reference of the highlighting), false to replace
/// the current dump only (refresh at the same stop: the reference stays the dump of the previous stop)
/// @param error Receives a message on failure (the dump is then cleared, the debugger stays usable)
static bool ReadMemoryZone(DebugLink &link, MemoryState &mem, int lines, bool bShift, char *error, size_t errorSize)
{
    error[0] = 0;
    if (lines > MEMORY_DUMP_MAX_BYTES / MEMORY_BYTES_PER_LINE)
    {
        lines = MEMORY_DUMP_MAX_BYTES / MEMORY_BYTES_PER_LINE;
    }
    if (lines <= 0 || !mem.hasAddress)
    {
        return true;
    }
    NormalizeMemoryAddress(mem, lines);
    uint16_t wordCount = (uint16_t)(lines * MEMORY_BYTES_PER_LINE / 2);
    uint16_t words[MEMORY_DUMP_MAX_BYTES / 2];
    if (!link.ReadMemory(mem.address, wordCount, words, false))
    {
        mem.dump.Clear();
        uint32_t last = mem.address + (uint32_t)wordCount * 2 - 1;
        if (link.GetLastError() == DebugLinkError::InvalidArgument)
        {
            snprintf(error, errorSize, "$%06lx-$%06lx: read refused (I/O zone $300000-$3FFFFF or BricoExt ports $C1FF00-03), scroll away",
                (unsigned long)mem.address, (unsigned long)last);
        }
        else
        {
            snprintf(error, errorSize, "$%06lx-$%06lx: memory read failed: %s", (unsigned long)mem.address, (unsigned long)last,
                DebugLink::GetErrorText(link.GetLastError()));
        }
        return false;
    }
    uint8_t bytes[MEMORY_DUMP_MAX_BYTES];
    for (int i = 0; i < wordCount; ++i)
    {
        bytes[i * 2] = (uint8_t)(words[i] >> 8);
        bytes[i * 2 + 1] = (uint8_t)(words[i] & 0xff);
    }
    if (bShift)
    {
        mem.dump.Push(mem.address, bytes, wordCount * 2);
    }
    else
    {
        mem.dump.SetCurrent(mem.address, bytes, wordCount * 2);
    }
    return true;
}

/// @brief Classic view: print the dump (hexadecimal + ASCII)
static void PrintMemoryClassic(const MemoryState &mem, int lines)
{
    for (int line = 0; line < lines; ++line)
    {
        uint32_t lineAddress = mem.address + (uint32_t)line * MEMORY_BYTES_PER_LINE;
        printf("%06lx: ", (unsigned long)(lineAddress & 0xffffff));
        char ascii[MEMORY_BYTES_PER_LINE + 1];
        for (int i = 0; i < MEMORY_BYTES_PER_LINE; ++i)
        {
            uint8_t value = 0;
            bool known = mem.dump.GetByte(lineAddress + i, &value);
            if (known)
            {
                printf("%02x", value);
            }
            else
            {
                printf("--");
            }
            if (!mem.words || (i & 1))
            {
                printf(" ");
            }
            ascii[i] = (known && value >= 0x20 && value < 0x7f) ? (char)value : '.';
            if (i == 7)
            {
                printf(" ");
            }
        }
        ascii[MEMORY_BYTES_PER_LINE] = 0;
        printf(" %s\n", ascii);
    }
}

/// @brief Background tasks while the user types in a prompt (the message line holds the prompt: no async messages)
static void IdleWhilePrompting()
{
    MassStorageManager::Loop();
    ROMUploadManager::Loop();
}

/// @brief State shared with the poll callback called by DebugLink::WaitStop
struct CommandDebug::WaitContext
{
    CommandDebug *self;
    DebugTuiView *view;
    DebugLink *link;
    const DebugFrame *pFrameToShow;  ///< Frame shown if the view is toggled while waiting (nullptr if none)
    uint32_t nUnrecognizedSeen;
    bool bBreakAllowed;              ///< The 68k is running after a resume: 'b' requests a break
};

/// @brief Time allowed for the 68k to stop after a break request when leaving the debugger
#define EXIT_BREAK_TIMEOUT_MS 500

/// @brief State of the poll callback used while stopping the 68k before exit
struct ExitWaitContext
{
    DebugTuiView *view;
    uint64_t deadlineUs;
};

/// @brief Poll callback of the wait for the final stop: background tasks only, keys are drained and ignored (no
/// second exit), gives up at the deadline
static bool PollExitWaiting(void *context)
{
    ExitWaitContext *ctx = (ExitWaitContext *)context;
    MassStorageManager::Loop();
    ROMUploadManager::Loop();
    Tui::KeyEvent key;
    while (Tui::ReadKey(&key)) { }
    return time_us_64() < ctx->deadlineUs;
}

/// @brief Address where a step over must stop, if the instruction at PC is a call, a trap or a loop (DBcc).
/// @param frame Current stop
/// @param pNext Receives PC + instruction size (24 bits)
/// @return false if a plain STEP must be used: other instruction, undecoded ("Unknown") or truncated, so that its size
/// is not reliable. TRAPV is the only exception: not decoded by OpcodeDecoder, but known to be one word.
static bool GetStepOverTarget(const DebugFrame *frame, uint32_t *pNext)
{
    static DisassembledInstruction instruction;     // Static: too large for the stack
    if (DisassembleFrame(frame, &instruction, 1) != 1 || instruction.wordCount < 1 || instruction.truncated)
    {
        return false;
    }
    InstructionFlow flow = ClassifyInstruction(instruction.words);
    if (flow != InstructionFlow::Call && flow != InstructionFlow::Trap && flow != InstructionFlow::Loop)
    {
        return false;
    }
    uint32_t size = (uint32_t)instruction.wordCount * 2;
    if (strcmp(instruction.text, "Unknown") == 0)
    {
        if (instruction.words[0] != 0x4e76)     // TRAPV
        {
            return false;
        }
        size = 2;
    }
    *pNext = (frame->pc + size) & 0xffffff;
    return true;
}

void CommandDebug::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandDebug::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandDebug::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandDebug::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    // Optional parameter: full screen view
    bool bTuiMode = false;
    if (param1 != NULL)
    {
        if (strcasecmp(param1, "tui") != 0 || param2 != NULL)
        {
            snprintf(returnMessage, messageMaxSize, "Usage: " COMMAND_NAME " [tui]");
            return false;
        }
        bTuiMode = true;
    }

    printf("Trying to detect a NeoGeo conventional system ROM to patch its Trace interrupt vector...\n");
    if (*(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_OFFICIAL_TRACE_VECTOR) == 0x00c0 && *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_OFFICIAL_TRACE_VECTOR + 2) == 0x0420)
    {
        printf("- ROM detected. Patching original Trace interrupt routine @ 0x0c0024 to point to the trace routine @ 0x%06x...\n", (unsigned)TRACE_ROUTINE_ADDRESS_68K);
        *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x0420) = 0x4ef9;        // jmp opcode
        *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x0420 + 2) = (uint16_t)(TRACE_ROUTINE_ADDRESS_68K >> 16);        // Trace routine address high
        *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x0420 + 4) = (uint16_t)(TRACE_ROUTINE_ADDRESS_68K & 0xffff);    // Trace routine address low
        printf("- Injecting trace routine in the random numbers area: overwriting image offsets 0x%05x-0x%05x (68k 0x%06x-0x%06x), %d bytes...\n",
            (unsigned)BIOS_PATCH_TRACE_ROUTINE_OFFSET, (unsigned)(BIOS_PATCH_TRACE_ROUTINE_OFFSET + TRACE_ROUTINE_SIZE_BYTES - 1),
            (unsigned)TRACE_ROUTINE_ADDRESS_68K, (unsigned)(TRACE_ROUTINE_ADDRESS_68K + TRACE_ROUTINE_SIZE_BYTES - 1), (int)TRACE_ROUTINE_SIZE_BYTES);
        for (int i = 0; i < sizeof(gwTraceRoutine) / sizeof(gwTraceRoutine[0]); ++i)
        {
            *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + BIOS_PATCH_TRACE_ROUTINE_OFFSET + i * 2) = gwTraceRoutine[i];
        }
        printf("- Patching first instruction (move #0x2700,SR becomes move #0xa700,SR) to force SR Trace flag...\n");
        // Read the reset vector
        uint32_t resetVector = (((*(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 4)) << 16) + *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 4 + 2)) & 0x1ffff;
        if (*(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + resetVector) == 0x4ef9)
        {
            printf("- Vector is pointing to a jump instruction, following the jump @ 0x%06x...\n", resetVector);
            uint32_t resetJumpAddress = (((*(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + resetVector + 2)) << 16) + *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + resetVector + 4)) & 0x1ffff;
            printf("- Patching the SR configuration instruction...\n");
            if (*(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + resetJumpAddress) == 0x46fc)
            {
                *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + resetJumpAddress + 2) |= 0x8000;
                printf("- Done\n");
            }
            else
            {
                snprintf(returnMessage, messageMaxSize, "- The first instruction found was not an SR config instruction. Aborting live patch.\n");
                return false;
            }
        }
        else
        {
            snprintf(returnMessage, messageMaxSize, "- Vector is not pointing to a jump instruction, as expected for a standard system ROM. Aborting live patch.\n");
            return false;
        }
    }
    else
    {
        printf("Found non conventional system ROM. Hoping Trace routine has been configured.\n");
    }

    DebugLink::ResetMailbox();

    printf("Resetting the NeoGeo board...\n");
    PIOManager::ForceWatchdogReset();

    // Drop whatever a previous handler (woken up by ResetMailbox) or the reset itself may have sent: words and EOT
    InputBuffer::Clear(true);
    {
        InputBuffer::CheckEOTSignalAndClearEOT();
        uint16_t tmp;
        while (InputBuffer::GetValue(&tmp)) { }
    }

    printf("Waiting for debug frame from remote. Press 'x' to exit at any time, 't' to switch to the %s view...\n", bTuiMode ? "classic" : "full screen");

    MemoryState memory;
    mHistory.Clear();
    mStop = StopInfo();
    DebugTuiView view;
    view.SetHistory(&mHistory);
    view.SetMemoryWordMode(memory.words);
    view.ShowMemory(&memory.dump, false, 0, nullptr);
    if (bTuiMode)
    {
        view.Enter();
        view.SetMessage("Waiting for a debug frame from the 68k...");
    }

    DebugLink link;
    DebugFrame frames[2];
    DebugFrame *pFrame = &frames[0];
    DebugFrame *pPreviousFrame = nullptr;
    uint32_t nStepCount = 0;
    uint16_t nFrameNumber = 0;  // Stop counter (+1 per stop)
    WaitContext waitContext = { this, &view, &link, nullptr, 0, false };

    auto showMessage = [&](const char *text, bool isError)
    {
        if (Tui::IsActive())
        {
            view.SetMessage(text, isError);
        }
        else
        {
            printf("%s\n", text);
        }
    };

    // Read the displayed memory zone and update the full screen panel (the classic view only dumps on demand).
    // A failure is shown in the panel and in the message line, it never stops the debugger.
    auto refreshMemory = [&](bool bShift)
    {
        if (!Tui::IsActive())
        {
            memory.dump.Clear();    // Not tracked in the classic view: no stale reference for a later highlighting
            return;
        }
        char error[160];
        error[0] = 0;
        view.SetMemoryEnabled(memory.hasAddress);   // Layout first: it gives the number of lines
        int lines = view.GetMemoryLines();
        if (lines <= 0)
        {
            memory.dump.Clear();    // Panel hidden: not read, so the next dump must not be compared with an old one
        }
        else
        {
            ReadMemoryZone(link, memory, lines, bShift, error, sizeof(error));
        }
        view.ShowMemory(&memory.dump, memory.hasAddress, memory.address, error[0] != 0 ? error : nullptr);
        if (error[0] != 0)
        {
            view.SetMessage(error, true);
        }
    };

    // 'm' (prompt for an address) and the scroll keys, while the 68k is stopped. Same behaviour in both views,
    // the classic one prints a MEMORY_CLASSIC_LINES lines dump.
    auto handleMemoryAction = [&](Action action, const DebugFrame *pShown)
    {
        char text[80];
        if (action == Action::Memory)
        {
            bool validated;
            if (Tui::IsActive())
            {
                validated = view.PromptInput("Memory address (hex, $hex, a0-a7, pc, a6-$10): ", text, sizeof(text), IdleWhilePrompting);
            }
            else
            {
                printf("Memory address: hex, $hex, 0xhex, d0-d7, a0-a7, sp, pc, optional +/-hex ('a0' = register, '$a0' = address).\n"
                       "Enter alone = monitor off, Esc = cancel.\naddress> ");
                Tui::Flush();
                validated = Tui::ReadLine(0, 0, 0, text, sizeof(text), IdleWhilePrompting);
            }
            if (!validated)
            {
                return;
            }
            if (text[0] != 0)
            {
                uint32_t value;
                if (!DebugExpression::Parse(text, pShown, &value))
                {
                    char error[120];
                    snprintf(error, sizeof(error), "Invalid address expression: '%.60s'", text);
                    showMessage(error, true);
                    return;
                }
                memory.address = value;
                memory.hasAddress = true;
            }
            else
            {
                // Empty input: disable the monitor (no more reads, panel hidden)
                memory.hasAddress = false;
                memory.dump.Clear();
                if (Tui::IsActive())
                {
                    view.ShowMemory(&memory.dump, false, 0, nullptr);
                }
                showMessage("Memory monitor disabled (press 'm' to enable it)", false);
                return;
            }
            if (Tui::IsActive())
            {
                view.SetMemoryEnabled(true);
                if (!view.IsMemoryPanelVisible())
                {
                    view.ToggleMemoryPanel();   // Small terminal: show the memory instead of the code
                }
            }
        }
        else
        {
            if (!memory.hasAddress)
            {
                showMessage("No memory address yet: press 'm' to enter one", false);
                return;
            }
            if (Tui::IsActive() && !view.IsMemoryPanelVisible())
            {
                view.ToggleMemoryPanel();
            }
            int lines = Tui::IsActive() ? view.GetMemoryLines() : MEMORY_CLASSIC_LINES;
            uint32_t delta = (action == Action::PageUp || action == Action::PageDown) ? (uint32_t)lines * MEMORY_BYTES_PER_LINE : MEMORY_BYTES_PER_LINE;
            if (action == Action::PageUp || action == Action::LineUp)
            {
                memory.address = memory.address >= delta ? memory.address - delta : 0;
            }
            else
            {
                memory.address += delta;    // Clamped by NormalizeMemoryAddress
            }
        }

        if (Tui::IsActive())
        {
            refreshMemory(false);
        }
        else
        {
            char error[160];
            if (ReadMemoryZone(link, memory, MEMORY_CLASSIC_LINES, false, error, sizeof(error)))
            {
                PrintMemoryClassic(memory, MEMORY_CLASSIC_LINES);
            }
            else
            {
                printf("%s\n", error);
            }
        }
    };

    while (true)
    {
        // Wait for a stop frame from the 68k...
        waitContext.pFrameToShow = nStepCount > 0 ? pPreviousFrame : nullptr;
        waitContext.bBreakAllowed = mStop.kind != ResumeKind::None && nStepCount > 0;
        DebugStop stop;
        if (!link.WaitStop(&stop, PollWaiting, &waitContext))
        {
            mHistory.Clear();
            // 'x' while a STEP / RUN_UNTIL is in progress: the 68k is still traced and would freeze in the handler at its
            // next stop. Ask for a break, wait for that stop and let it go untraced (CONTINUE). Not needed after
            // CONTINUE (T already 0) nor before the first frame (nothing resumed yet).
            bool bLeftTraced = false;
            bool bTraced = nStepCount > 0 && (mStop.kind == ResumeKind::RunUntil || mStop.kind == ResumeKind::Step);
            if (bTraced && link.GetLastError() == DebugLinkError::Aborted)
            {
                if (Tui::IsActive())
                {
                    view.SetMessage("Exit: stopping the 68k...");
                }
                else
                {
                    printf("> stopping the 68k before exit...\n");
                }
                link.RequestBreak();
                ExitWaitContext exitContext = { &view, time_us_64() + EXIT_BREAK_TIMEOUT_MS * 1000 };
                DebugStop exitStop;
                if (link.WaitStop(&exitStop, PollExitWaiting, &exitContext))
                {
                    link.Resume(DebugCommand::Continue);
                }
                else
                {
                    bLeftTraced = true;
                }
            }
            view.Leave();
            if (bLeftTraced)
            {
                snprintf(returnMessage, messageMaxSize, "User interrupted, but the 68k did not stop: it may still be in trace mode (reset the board or run Dbg again)");
            }
            else
            {
                snprintf(returnMessage, messageMaxSize, "User interrupted debug frame download");
            }
            InputBuffer::Clear(true);
            return false;
        }

        // Stopped: read the registers, then the code at PC
        DebugRegisters regs;
        if (!link.GetRegisters(&regs))
        {
            mHistory.Clear();
            ReportLinkError(view, returnMessage, messageMaxSize, "Unable to read the registers", link);
            return false;
        }
        for (int i = 0; i < 8; ++i)
        {
            pFrame->d[i] = regs.d[i];
            pFrame->a[i] = i < 7 ? regs.a[i] : regs.GetA7();
        }
        pFrame->ssp = regs.ssp;
        pFrame->usp = regs.usp;
        pFrame->sr = regs.sr;
        pFrame->pc = regs.pc;
        pFrame->frameNumber = ++nFrameNumber;
        if (!link.ReadMemory(regs.pc, DEBUG_FRAME_CODE_WORDS, pFrame->code, false))
        {
            if (link.GetLastError() != DebugLinkError::InvalidArgument)
            {
                mHistory.Clear();
                ReportLinkError(view, returnMessage, messageMaxSize, "Unable to read the code at PC", link);
                return false;
            }
            // Code not readable (e.g. PC in the $300000-$3FFFFF zone): display empty code
            memset(pFrame->code, 0, sizeof(pFrame->code));
        }
        ++nStepCount;

        // How was this stop reached? For a RUN_UNTIL: on its target (PC == target and SSP >= minimum SP, the 68k
        // condition) or elsewhere (break, other breakpoint / trace event).
        // T10 hook: here call CallStack::OnStep (kind Step), OnRunUntilReached(pFrame->a[7]) (kind RunUntil and
        // bReachedTarget), Invalidate() (Continue, 'g', RunUntil not reached). The previous stop is pPreviousFrame.
        mStop.bReachedTarget = mStop.kind == ResumeKind::RunUntil && regs.pc == mStop.runUntilTarget && regs.ssp >= mStop.runUntilMinSp;

        // History: after a STEP, or a step over stopped on its target, the instruction executed is the first one of the
        // previous stop (same words and text, no extra memory read): after a step over it is marked as a flow break so
        // that the skipped jsr / bsr / dbcc is displayed as a "from" line. After anything else (first stop, continue,
        // 'g', break, RUN_UNTIL stopped elsewhere) there is no history.
        bool bStepOverReached = mStop.kind == ResumeKind::RunUntil && mStop.bStepOver && mStop.bReachedTarget;
        if ((mStop.kind != ResumeKind::Step && !bStepOverReached) || pPreviousFrame == nullptr)
        {
            mHistory.Clear();
        }
        else
        {
            // Static: too large for the stack
            static DisassembledInstruction executed;
            if (DisassembleFrame(pPreviousFrame, &executed, 1) == 1)
            {
                mHistory.Push(executed, pPreviousFrame->a[7], bStepOverReached);
            }
        }

        // Display the frame. The full screen view always keeps track of it (it only draws when active),
        // so that switching views at any time shows the current frame
        view.ShowFrame(pFrame, pPreviousFrame, nStepCount);
        if (Tui::IsActive())
        {
            view.SetMessage("");
        }
        else
        {
            ShowFrameClassic(pFrame);
        }
        refreshMemory(true);

        // Wait for a user command
        bool bWaiting = true;
        DebugCommand debugCommand = DebugCommand::Step;
        uint32_t runUntilTarget = 0;
        uint32_t runUntilMinSp = 0;
        bool bStepOver = false;
        while (bWaiting)
        {
            ServiceBackgroundTasks(view);
            Action action = ReadAction();
            switch (action)
            {
            case Action::Step:
                if (!Tui::IsActive())
                {
                    printf("> step\n");
                }
                debugCommand = DebugCommand::Step;
                bWaiting = false;
                break;
            case Action::StepOver:
            {
                uint32_t next;
                if (GetStepOverTarget(pFrame, &next))
                {
                    debugCommand = DebugCommand::RunUntil;
                    runUntilTarget = next;
                    runUntilMinSp = pFrame->ssp;    // The 68k compares the SSP before the exception
                    bStepOver = true;
                    if (!Tui::IsActive())
                    {
                        printf("> step over, running until $%06lx ('b' to break, 'x' to exit)...\n", (unsigned long)next);
                    }
                }
                else
                {
                    debugCommand = DebugCommand::Step;
                    if (!Tui::IsActive())
                    {
                        printf("> step\n");
                    }
                }
                bWaiting = false;
                break;
            }
            case Action::RunTo:
            {
                char text[80];
                bool validated;
                if (Tui::IsActive())
                {
                    validated = view.PromptInput("Run to address (hex, $hex, a0-a7, pc, a6-$10): ", text, sizeof(text), IdleWhilePrompting);
                }
                else
                {
                    printf("Run to address: hex, $hex, 0xhex, d0-d7, a0-a7, sp, pc, optional +/-hex ('a0' = register, '$a0' = address).\n"
                           "Enter alone or Esc = cancel.\naddress> ");
                    Tui::Flush();
                    validated = Tui::ReadLine(0, 0, 0, text, sizeof(text), IdleWhilePrompting);
                }
                if (!validated || text[0] == 0)
                {
                    break;
                }
                uint32_t value;
                if (!DebugExpression::Parse(text, pFrame, &value))
                {
                    char error[120];
                    snprintf(error, sizeof(error), "Invalid address expression: '%.60s'", text);
                    showMessage(error, true);
                    break;
                }
                value &= 0xffffff;
                if (value & 1)
                {
                    showMessage("Odd address: the PC is always even", true);
                    break;
                }
                debugCommand = DebugCommand::RunUntil;
                runUntilTarget = value;
                runUntilMinSp = 0;
                bStepOver = false;
                if (!Tui::IsActive())
                {
                    printf("> run to $%06lx ('b' to break, 'x' to exit)...\n", (unsigned long)value);
                }
                bWaiting = false;
                break;
            }
            case Action::Continue:
                if (Tui::IsActive())
                {
                    view.SetMessage("Running... 'b' = break (needs the 68k in trace mode), 'x' = exit");
                }
                else
                {
                    printf("> continue ('b' to break, 'x' to exit)...\n");
                }
                debugCommand = DebugCommand::Continue;
                bWaiting = false;
                break;
            case Action::Exit:
                // The 68k is blocked in the handler: let it go on instead of leaving it frozen
                link.Resume(DebugCommand::Continue);
                view.Leave();
                snprintf(returnMessage, messageMaxSize, "Debug interrupted");
                return true;
            case Action::ToggleView:
                ToggleView(view, pFrame, true);
                refreshMemory(false);
                break;
            case Action::Redraw:
                view.Redraw();
                refreshMemory(false);   // The number of lines may have changed
                break;
            case Action::Resize:
                view.Redraw(false);
                refreshMemory(false);
                break;
            case Action::ToggleWords:
                if (!memory.hasAddress)
                {
                    showMessage("No memory address yet: press 'm' to enter one", false);
                    break;
                }
                memory.words = !memory.words;
                if (Tui::IsActive())
                {
                    view.SetMemoryWordMode(memory.words);
                }
                else
                {
                    PrintMemoryClassic(memory, MEMORY_CLASSIC_LINES);   // Re-display the current dump
                }
                break;
            case Action::ToggleCode:
                if (!memory.hasAddress)
                {
                    showMessage("No memory address yet: press 'm' to enter one", false);
                }
                else if (Tui::IsActive() && view.ToggleMemoryPanel())
                {
                    refreshMemory(false);
                }
                break;
            case Action::Memory:
            case Action::PageUp:
            case Action::PageDown:
            case Action::LineUp:
            case Action::LineDown:
                handleMemoryAction(action, pFrame);
                break;
            default:
                break;
            }
        }

        // The displayed frame becomes the reference for highlighting changes
        pPreviousFrame = pFrame;
        pFrame = (pFrame == &frames[0]) ? &frames[1] : &frames[0];
        bool bRunning = debugCommand != DebugCommand::Step;
        view.SetRunTarget(debugCommand == DebugCommand::RunUntil, runUntilTarget);
        view.SetState(bRunning ? DebugTuiView::State::Running : DebugTuiView::State::Waiting);
        if (debugCommand == DebugCommand::RunUntil && Tui::IsActive())
        {
            view.SetMessage("Running... 'b' = break, 'x' = exit");
        }

        // What the next stop will be the consequence of. The history is kept until that stop (STEP and step over: the
        // executed instruction is pushed there), it is cleared there for anything else.
        mStop = StopInfo();
        mStop.kind = debugCommand == DebugCommand::Step ? ResumeKind::Step :
            (debugCommand == DebugCommand::RunUntil ? ResumeKind::RunUntil : ResumeKind::Continue);
        mStop.bStepOver = bStepOver;
        mStop.runUntilTarget = runUntilTarget;
        mStop.runUntilMinSp = runUntilMinSp;
        if (mStop.kind == ResumeKind::Continue || (mStop.kind == ResumeKind::RunUntil && !bStepOver))
        {
            mHistory.Clear();
        }

        // Send the command to the 68k
        link.Resume(debugCommand, runUntilTarget, runUntilMinSp);
    }

    return true;
}

/// @brief Called by DebugLink::WaitStop while waiting for a stop frame. Returns false to abandon the wait.
bool CommandDebug::PollWaiting(void *context)
{
    WaitContext *ctx = (WaitContext *)context;
    ServiceBackgroundTasks(*ctx->view);

    // Non blocking warning when the 68k sent a frame we do not understand.
    // Only meaningful for a 59 words frame (v1 handler signature), and never with a BricoNeo/Brico68k
    // system ROM: its traced code legitimately sends BricoExt frames of any length.
    uint32_t unrecognized = ctx->link->GetUnrecognizedFrameCount();
    if (unrecognized != ctx->nUnrecognizedSeen)
    {
        ctx->nUnrecognizedSeen = unrecognized;
        if (BricoExtManager::IsBricoNeoROM() || ctx->link->GetLastUnrecognizedWordCount() != DEBUG_V1_FRAME_WORDS)
        {
            // no message
        }
        else if (Tui::IsActive())
        {
            ctx->view->SetMessage(MESSAGE_UNRECOGNIZED_FRAME, true);
        }
        else
        {
            printf("%s\n", MESSAGE_UNRECOGNIZED_FRAME);
        }
    }

    switch (ReadAction())
    {
    case Action::Exit:
        return false;
    case Action::ToggleView:
        ctx->self->ToggleView(*ctx->view, ctx->pFrameToShow, false);
        break;
    case Action::Redraw:
        ctx->view->Redraw();
        break;
    case Action::Resize:
        ctx->view->Redraw(false);
        break;
    case Action::Break:
        if (ctx->bBreakAllowed)
        {
            ctx->link->RequestBreak();
            if (Tui::IsActive())
            {
                ctx->view->SetMessage("Break requested: waiting for the 68k to enter the handler...");
            }
            else
            {
                printf("> break requested...\n");
            }
        }
        break;
    default:
        break;
    }
    return true;
}

/// @brief Report a link error (response timeout, short response...) in both views and prepare the command return message
void CommandDebug::ReportLinkError(DebugTuiView &view, char *returnMessage, int messageMaxSize, const char *what, const DebugLink &link)
{
    char text[100];
    snprintf(text, sizeof(text), "%s: %s", what, DebugLink::GetErrorText(link.GetLastError()));
    if (Tui::IsActive())
    {
        view.SetMessage(text, true);
    }
    view.Leave();
    snprintf(returnMessage, messageMaxSize, "%s", text);
}

void CommandDebug::ServiceBackgroundTasks(DebugTuiView &view)
{
    MassStorageManager::Loop();
    ROMUploadManager::Loop();
    if (Tui::IsActive())
    {
        view.PollAsyncMessages();
    }
}

/// @brief Read a key and translate it into a debugger action. Same keys in both views.
CommandDebug::Action CommandDebug::ReadAction()
{
    Tui::KeyEvent key;
    if (!Tui::ReadKey(&key))
    {
        return Action::None;
    }
    switch (key.key)
    {
    case Tui::Key::Enter:
        return Action::Step;
    case Tui::Key::F5:
        return Action::Continue;
    case Tui::Key::F10:
        return Action::StepOver;
    case Tui::Key::F2:
        return Action::ToggleView;
    case Tui::Key::F3:
        return Action::ToggleCode;
    case Tui::Key::PageUp:
        return Action::PageUp;
    case Tui::Key::PageDown:
        return Action::PageDown;
    case Tui::Key::Up:
        return Action::LineUp;
    case Tui::Key::Down:
        return Action::LineDown;
    case Tui::Key::Escape:
        return Action::Exit;
    case Tui::Key::Resize:
        return Action::Resize;
    case Tui::Key::Char:
        switch (key.c)
        {
        case 's': case 'S': case ' ':
            return Action::Step;
        case 'o': case 'O':
            return Action::StepOver;
        case 'g': case 'G':
            return Action::RunTo;
        case 'b': case 'B':
            return Action::Break;
        case 'c': case 'C':
            return Action::Continue;
        case 'x': case 'X': case 'q': case 'Q':
            return Action::Exit;
        case 't': case 'T':
            return Action::ToggleView;
        case 'r': case 'R':
            return Action::Redraw;
        case 'm': case 'M':
            return Action::Memory;
        case 'w': case 'W':
            return Action::ToggleWords;
        default:
            return Action::None;
        }
    default:
        return Action::None;
    }
}

/// @brief Switch between the classic and the full screen views
/// @param pFrame Frame currently displayed (nullptr if none)
/// @param bWaitingForCommand true if the user is expected to step / continue
void CommandDebug::ToggleView(DebugTuiView &view, const DebugFrame *pFrame, bool bWaitingForCommand)
{
    if (Tui::IsActive())
    {
        // Back to the classic console, as it was when the full screen view was entered
        view.Leave();
        if (pFrame != nullptr && bWaitingForCommand)
        {
            ShowFrameClassic(pFrame);
        }
        else
        {
            printf("Waiting for debug frame from remote. Press 'x' to exit, 't' for the full screen view...\n");
        }
    }
    else
    {
        view.Enter();
    }
}

/// @brief Classic (scrolling) display of a debug frame
void CommandDebug::ShowFrameClassic(const DebugFrame *frame)
{
    // Display splitter
    ShowSplitter();

    // Display register values
    printf("\n");
    uint16_t statusReg = frame->sr;
    uint32_t programcounter = frame->pc;
    for (int i = 0; i < 8; ++i)
    {
        unsigned long addressRegister = frame->a[i];  // a[7] already holds the effective a7
        printf("|  d%d: %08lx   |   a%d: %08lx   |  ", i, (unsigned long)frame->d[i], i, addressRegister);
        switch (i)
        {
        case 1:
            printf("SR: %04x", statusReg);
            RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - 49);
            printf("|");
            break;
        case 2:
            printf("    T S IRQ | XNZVC");
            RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - 60);
            printf("|");
            break;
        case 3:
            printf("    %d %d %d%d%d | %d%d%d%d%d",
                (statusReg & (1 << 15)) ? 1 : 0,  // T
                (statusReg & (1 << 13)) ? 1 : 0,  // S
                (statusReg & (1 << 10)) ? 1 : 0,  // IRQ
                (statusReg & (1 << 9)) ? 1 : 0,  // IRQ
                (statusReg & (1 << 8)) ? 1 : 0,  // IRQ
                (statusReg & (1 << 4)) ? 1 : 0,  // X
                (statusReg & (1 << 3)) ? 1 : 0,  // N
                (statusReg & (1 << 2)) ? 1 : 0,  // Z
                (statusReg & (1 << 1)) ? 1 : 0,  // V
                (statusReg & (1 << 0)) ? 1 : 0  // C
            );
            RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - 60);
            printf("|");
            break;
        case 5:
            printf("PC: %07lx", (unsigned long)programcounter);
            RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - 52);
            printf("|");
            break;
        default:
            RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - 41);
            printf("|");
            break;
        }
        printf("\n");
    }

    ShowSplitter();

    printf("\n| Program:");
    RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - 11);
    printf("|\n");
    DisassembledInstruction instructions[CONSOLE_INSTRUCTIONS_COUNT];
    int count = DisassembleFrame(frame, instructions, CONSOLE_INSTRUCTIONS_COUNT);
    // Executed instructions before the current one (contiguous lines, or a single "from" line after a flow break)
    HistoryRow historyRows[ExecutionHistory::MAX_ROWS];
    int historyCount = mHistory.BuildRows(frame->pc, historyRows);
    for (int i = 0; i < historyCount; ++i)
    {
        PrintInstructionClassic(&historyRows[i].pInstruction->instruction, historyRows[i].isFromLine ? "| <- " : "|    ");
    }
    for (int i = 0; i < count; ++i)
    {
        PrintInstructionClassic(&instructions[i], i == 0 ? "| -> " : "|    ");
    }

    ShowSplitter();
    printf("\n's'/space step, 'o' step over, 'c' continue, 'g' run to address, 'm' memory dump (PgUp/PgDn scroll, 'w' 8/16-bit), 't' full screen view, 'x' exit\n");
}

void CommandDebug::PrintInstructionClassic(const DisassembledInstruction *instruction, const char *prefix)
{
    // The branch target "($xxxxxx)" is appended to the text only if it fits in the text field, otherwise omitted
    char buffer[DEBUG_INSTRUCTION_MAX_TEXT + 16];
    const char *text = instruction->truncated ? "..." : instruction->text;
    if (instruction->hasTarget && strlen(text) + 10 <= CONSOLE_MAX_INSTRUCTIONS_LENGTH)
    {
        snprintf(buffer, sizeof(buffer), "%s ($%06lx)", text, (unsigned long)(instruction->target & 0xffffff));
        text = buffer;
    }
    printf("%s", prefix);
    printf("%07lx: ", (unsigned long)instruction->address);
    printf("%s ", text);
    RepeatChars(' ', CONSOLE_MAX_INSTRUCTIONS_LENGTH - strlen(text));
    printf("| ");

    for (int w = 0; w < instruction->wordCount; ++w)
    {
        printf("%04x ", instruction->words[w]);
    }

    RepeatChars(' ', CONSOLE_MAX_ROW_LENGTH - CONSOLE_MAX_INSTRUCTIONS_LENGTH - 18 - instruction->wordCount * 5);

    printf("|\n");
}

void CommandDebug::ShowSplitter()
{
    RepeatChars('-', CONSOLE_MAX_ROW_LENGTH);
}

void CommandDebug::RepeatChars(char c, int16_t count)
{
    for (int i = 0; i < count; ++i)
    {
        putchar_raw(c);
    }
}

