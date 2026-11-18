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
#include "DebugProtocol.h"
#include "InputBuffer.h"
#include "MassStorageManager.h"
#include "PIOManager.h"
#include "ROMImageManager.h"
#include "ROMUploadManager.h"
#include "Tui.h"
#include "TraceRoutine.h"

#define COMMAND_NAME "Dbg"

#define COMMAND_DESC_PARAMETERS "[tui]"
#define COMMAND_DESC_TEXT "Enter 68000 debug mode. Need to activate trace mode in 68k code and declare TRACE interrupt vector. 'tui' for the full screen view"

static_assert(TRACE_ROUTINE_SIZE_BYTES <= BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES, "TRACE handler too big for the BIOS patch area");
static_assert(sizeof(gwTraceRoutine) == TRACE_ROUTINE_SIZE_BYTES, "Inconsistent TraceRoutine.h");

#define TRACE_ROUTINE_ADDRESS_68K (0xC00000u + BIOS_PATCH_TRACE_ROUTINE_OFFSET)

/// @brief Size in words of the v1 stop frame (only used to recognise an old handler)
#define DEBUG_V1_FRAME_WORDS 59

#define MESSAGE_UNRECOGNIZED_FRAME "Unrecognized frame received - TRACE handler from another version?"

/// @brief State shared with the poll callback called by DebugLink::WaitStop
struct CommandDebug::WaitContext
{
    CommandDebug *self;
    DebugTuiView *view;
    DebugLink *link;
    const DebugFrame *pFrameToShow;  ///< Frame shown if the view is toggled while waiting (nullptr if none)
    uint32_t nUnrecognizedSeen;
};

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

    DebugTuiView view;
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
    WaitContext waitContext = { this, &view, &link, nullptr, 0 };

    while (true)
    {
        // Wait for a stop frame from the 68k...
        waitContext.pFrameToShow = nStepCount > 0 ? pPreviousFrame : nullptr;
        DebugStop stop;
        if (!link.WaitStop(&stop, PollWaiting, &waitContext))
        {
            view.Leave();
            snprintf(returnMessage, messageMaxSize, "User interrupted debug frame download");
            InputBuffer::Clear(true);
            return false;
        }

        // Stopped: read the registers, then the code at PC
        DebugRegisters regs;
        if (!link.GetRegisters(&regs))
        {
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
                ReportLinkError(view, returnMessage, messageMaxSize, "Unable to read the code at PC", link);
                return false;
            }
            // Code not readable (e.g. PC in the $300000-$3FFFFF zone): display empty code
            memset(pFrame->code, 0, sizeof(pFrame->code));
        }
        ++nStepCount;

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

        // Wait for a user command
        bool bWaiting = true;
        DebugCommand debugCommand = DebugCommand::Step;
        while (bWaiting)
        {
            ServiceBackgroundTasks(view);
            switch (ReadAction())
            {
            case Action::Step:
                if (!Tui::IsActive())
                {
                    printf("> step\n");
                }
                debugCommand = DebugCommand::Step;
                bWaiting = false;
                break;
            case Action::Continue:
                if (Tui::IsActive())
                {
                    view.SetState(DebugTuiView::State::Running);
                    view.SetMessage("Running... the view will be updated if the 68k enters trace mode again");
                }
                else
                {
                    printf("> continue...\n");
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
                break;
            case Action::Redraw:
                view.Redraw();
                break;
            case Action::Resize:
                view.Redraw(false);
                break;
            default:
                break;
            }
        }

        // The displayed frame becomes the reference for highlighting changes
        pPreviousFrame = pFrame;
        pFrame = (pFrame == &frames[0]) ? &frames[1] : &frames[0];
        view.SetState(debugCommand == DebugCommand::Continue ? DebugTuiView::State::Running : DebugTuiView::State::Waiting);

        // Send the command to the 68k
        link.Resume(debugCommand);
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
    case Tui::Key::F2:
        return Action::ToggleView;
    case Tui::Key::Escape:
        return Action::Exit;
    case Tui::Key::Resize:
        return Action::Resize;
    case Tui::Key::Char:
        switch (key.c)
        {
        case 's': case 'S': case ' ':
            return Action::Step;
        case 'c': case 'C':
            return Action::Continue;
        case 'x': case 'X': case 'q': case 'Q':
            return Action::Exit;
        case 't': case 'T':
            return Action::ToggleView;
        case 'r': case 'R':
            return Action::Redraw;
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
    for (int i = 0; i < count; ++i)
    {
        const DisassembledInstruction *instruction = &instructions[i];
        const char *text = instruction->truncated ? "..." : instruction->text;
        printf(i == 0 ? "| -> ": "|    ");
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

    ShowSplitter();
    printf("\n's' or space to step, 'c' to continue, 't' for the full screen view, 'x' to exit\n");
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

