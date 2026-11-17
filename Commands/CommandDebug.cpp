#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define CONSOLE_MAX_INSTRUCTIONS_LENGTH 22
#define CONSOLE_MAX_ROW_LENGTH 64
#define CONSOLE_INSTRUCTIONS_COUNT 5

#include "CommandDebug.h"
#include "CommandManager.h"
#include "InputBuffer.h"
#include "MassStorageManager.h"
#include "PIOManager.h"
#include "ROMImageManager.h"
#include "ROMUploadManager.h"
#include "Tui.h"

#define COMMAND_NAME "Dbg"

#define COMMAND_DESC_PARAMETERS "[tui]"
#define COMMAND_DESC_TEXT "Enter 68000 debug mode. Need to activate trace mode in 68k code and declare TRACE interrupt vector. 'tui' for the full screen view"

#define FRAME_SIZE_REGISTER 2
#define FRAME_SIZE_WORD 1
#define FRAME_SIZE (FRAME_SIZE_REGISTER) * 8 * 2 /* Data and address registers */ \
                    + FRAME_SIZE_WORD /* SR register */ \
                    + FRAME_SIZE_REGISTER /* PC register */ \
                    + 25 * FRAME_SIZE_WORD /* PC memory neighbourhood */ \
                    + FRAME_SIZE_WORD /* Frame counter */

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

    // Clear the input buffer
    InputBuffer::CheckEOTSignalAndClearEOT();
    uint16_t tmp;
    while(InputBuffer::GetValue(&tmp));

    printf("Trying to detect a NeoGeo conventional system ROM to patch its Trace interrupt vector...\n");
    if (*(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_OFFICIAL_TRACE_VECTOR) == 0x00c0 && *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_OFFICIAL_TRACE_VECTOR + 2) == 0x0420)
    {
        printf("- ROM detected. Patching original Trace interrupt routine @ 0x0c0024 to point to random numbers area @ 0x0c4200...\n");
        *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x0420) = 0x4ef9;        // jmp opcode
        *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x0420 + 2) = 0x00c0;        // Random numbers high
        *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x0420 + 4) = 0x4200;    // Random numbers low
        printf("- Injecting trace routine in the random numbers area...\n");
        for (int i = 0; i < sizeof(gwTraceRoutine) / sizeof(gwTraceRoutine[0]); ++i)
        {
            *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + 0x4200 + i * 2) = gwTraceRoutine[i];
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

    printf("Resetting the NeoGeo board...\n");
    PIOManager::ForceWatchdogReset();

    printf("Waiting for debug frame from remote. Press 'x' to exit at any time, 't' to switch to the %s view...\n", bTuiMode ? "classic" : "full screen");

    DebugTuiView view;
    if (bTuiMode)
    {
        view.Enter();
        view.SetMessage("Waiting for a debug frame from the 68k...");
    }

    DebugFrame frames[2];
    DebugFrame *pFrame = &frames[0];
    DebugFrame *pPreviousFrame = nullptr;
    uint32_t nStepCount = 0;

    while (true)
    {
        // Wait for remote information...
        while (!InputBuffer::CheckEOTSignalAndClearEOT())
        {
            ServiceBackgroundTasks(view);
            switch (ReadAction())
            {
            case Action::Exit:
                view.Leave();
                snprintf(returnMessage, messageMaxSize, "User interrupted debug frame download");
                InputBuffer::Clear(true);
                return false;
            case Action::ToggleView:
                ToggleView(view, nStepCount > 0 ? pPreviousFrame : nullptr, false);
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

        // Stopped waiting
        // Frame received, read it
        uint16_t frameContentRaw[FRAME_SIZE];
        for (int i = 0; i < FRAME_SIZE; ++i)
        {
            if (!InputBuffer::GetValue(frameContentRaw + i))
            {
                view.Leave();
                snprintf(returnMessage, messageMaxSize, "Incorrect frame data size received");
                return false;
            }
        }
        DecodeFrame(frameContentRaw, pFrame);
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
        uint16_t debugCommand = 0;
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
                debugCommand = 0;
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
                debugCommand = 1;
                bWaiting = false;
                break;
            case Action::Exit:
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
        view.SetState(debugCommand == 1 ? DebugTuiView::State::Running : DebugTuiView::State::Waiting);

        // Send command
        ROMImageManager::GetImageBaseAddressB()[0x1fffc] = (uint8_t)debugCommand;
        ROMImageManager::GetImageBaseAddressB()[0x1fffd] = (uint8_t)(debugCommand >> 8);

        // Send back incremented frame number to send the debug command to the 68k
        uint16_t frameNumber = pPreviousFrame->frameNumber + 1;
        ROMImageManager::GetImageBaseAddressB()[0x1fffe] = (uint8_t)frameNumber;
        ROMImageManager::GetImageBaseAddressB()[0x1ffff] = (uint8_t)(frameNumber >> 8);
    }

    return true;
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

void CommandDebug::DecodeFrame(const uint16_t *raw, DebugFrame *frame)
{
    for (int i = 0; i < 8; ++i)
    {
        frame->d[i] = ((uint32_t)raw[i * 2] << 16) | raw[i * 2 + 1];
        frame->a[i] = ((uint32_t)raw[i * 2 + 16] << 16) | raw[i * 2 + 17];
    }
    frame->sr = raw[FRAME_SIZE_REGISTER * 8 * 2];
    frame->pc = ((uint32_t)raw[FRAME_SIZE_REGISTER * 8 * 2 + 1] << 16) | raw[FRAME_SIZE_REGISTER * 8 * 2 + 2];
    memcpy(frame->code, raw + FRAME_SIZE_REGISTER * 8 * 2 + 3, sizeof(frame->code));
    frame->frameNumber = raw[FRAME_SIZE - 1];
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
        printf("|  d%d: %08lx   |   a%d: %08lx   |  ", i, (unsigned long)frame->d[i], i, (unsigned long)frame->a[i]);
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


uint16_t CommandDebug::gwTraceRoutine[] =
{
    0x13c0, 0x0030, 0x0001, 0x48e7, 0xffff, 0x3e3c, 0x001f, 0x3c3c, 0x0000, 0x3037, 0x6000, 0x616c, 0x5446, 0x51cf, 0xfff6, 0x302f, 0x0040, 0x6160, 0x302f, 0x0042, 0x615a, 0x302f, 0x0044, 0x6154, 0x302f, 0x0042, 0x4840, 0x302f, 0x0044, 0x2040, 0x3e3c, 0x0018, 0x3018, 0x6140, 0x51cf, 0xfffa, 0x3039, 0x00c1, 0xfffe, 0x6134, 0x3e39, 0x00c1, 0xff00, 0x13c0, 0x0030, 0x0001, 0x3239, 0x00c1, 0xfffe, 0xb240, 0x67f0, 0x3039, 0x00c1, 0xfffc, 0x4a40, 0x670e, 0xb07c, 0x0001, 0x6702, 0x6006, 0x026f, 0x7fff, 0x0040, 0x4cdf, 0xffff, 0x4e73, 0x48e7, 0xc080, 0x40c1, 0xc0bc, 0x0000, 0xffff, 0xe388, 0xd0bc, 0x00c2, 0x0000, 0xc188, 0x46fc, 0x2700, 0x3039, 0x00c1, 0xff02, 0x3010, 0x46c1, 0x4cdf, 0x0103, 0x4e75
};