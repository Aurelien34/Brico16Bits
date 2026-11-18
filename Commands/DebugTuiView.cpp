#include <stdio.h>
#include <string.h>

#include "DebugTuiView.h"
#include "CommandManager.h"
#include "Tui.h"

// Layout: designed for 80x24 at least, stretched to the terminal size (see ComputeLayout())
#define ROW_TITLE           1
#define ROW_BOXES           3
#define HEIGHT_BOXES        10
#define COL_REGISTERS       1
#define ROW_CODE            (ROW_BOXES + HEIGHT_BOXES)
#define CODE_TEXT_MIN_WIDTH 36
#define CODE_MAX_LINES      DEBUG_FRAME_CODE_WORDS     // At most one instruction per code word

// Colors (xterm 256 colors palette)
#define COLOR_BAR_FG        231
#define COLOR_BAR_BG        24
#define COLOR_BAR_KEY       226
#define COLOR_BORDER        244
#define COLOR_LABEL         45
#define COLOR_VALUE         252
#define COLOR_VALUE_CHANGED 226
#define COLOR_VALUE_UNKNOWN 240
#define COLOR_FLAG_ON       46
#define COLOR_FLAG_OFF      240
#define COLOR_CODE_ADDRESS  110
#define COLOR_CODE_TEXT     252
#define COLOR_CODE_HEX      244
#define COLOR_CURRENT_BG    237
#define COLOR_CURRENT_ARROW 46
#define COLOR_MESSAGE       214
#define COLOR_ERROR         196
#define COLOR_STATE_WAITING 130
#define COLOR_STATE_STOPPED 124
#define COLOR_STATE_RUNNING 28

void DebugTuiView::Enter()
{
    // Tui::Enter() clears the screen and queries the terminal size
    Tui::Enter();
    DrawAll();
}

void DebugTuiView::Leave()
{
    Tui::Leave();
}

/// @brief Clear and redraw the whole screen
/// @param querySize true to query the terminal size again (not needed after a Tui::Key::Resize, the size is known)
void DebugTuiView::Redraw(bool querySize)
{
    if (!Tui::IsActive())
    {
        return;
    }
    Tui::Clear();
    if (querySize)
    {
        Tui::QuerySize();
    }
    DrawAll();
}

/// @brief Compute the position of the screen elements from the terminal size (at least MIN_COLS x MIN_ROWS):
/// boxes are stretched horizontally, the program box takes all the remaining height
void DebugTuiView::ComputeLayout()
{
    mnScreenWidth = Tui::GetCols();
    mnScreenHeight = Tui::GetRows();

    // Registers and status boxes share the width (39 / 41 at 80 columns)
    mnWidthRegisters = mnScreenWidth * 39 / 80;
    mnColStatus = COL_REGISTERS + mnWidthRegisters;
    mnWidthStatus = mnScreenWidth - mnWidthRegisters;
    mnColAddressRegisters = mnWidthRegisters / 2 + 2;

    // Message and help lines at the bottom, program box in between
    mnRowHelp = mnScreenHeight;
    mnRowMessage = mnScreenHeight - 1;
    mnHeightCode = mnRowMessage - ROW_CODE;
    mnCodeLines = mnHeightCode - 2;
    if (mnCodeLines > CODE_MAX_LINES)
    {
        mnCodeLines = CODE_MAX_LINES;
    }
}

void DebugTuiView::DrawAll()
{
    ComputeLayout();
    if (!Tui::IsLargeEnough())
    {
        DrawTooSmall();
    }
    else
    {
        DrawStatic();
        DrawTitle();
        DrawRegisters();
        DrawStatus();
        DrawCode();
        DrawMessage();
        DrawHelp();
    }
    Tui::Flush();
}

/// @brief Display a new frame. Registers and flags that changed since previousFrame (if any) are highlighted.
/// The frames must stay valid as long as they are displayed.
void DebugTuiView::ShowFrame(const DebugFrame *frame, const DebugFrame *previousFrame, uint32_t stepCount)
{
    mpFrame = frame;
    mpPreviousFrame = previousFrame;
    mnStepCount = stepCount;
    meState = State::Stopped;
    if (!Tui::IsActive() || !Tui::IsLargeEnough())
    {
        return;
    }
    DrawTitle();
    DrawRegisters();
    DrawStatus();
    DrawCode();
    Tui::Flush();
}

void DebugTuiView::SetState(State state)
{
    meState = state;
    if (Tui::IsActive() && Tui::IsLargeEnough())
    {
        DrawTitle();
        Tui::Flush();
    }
}

void DebugTuiView::SetMessage(const char *message, bool isError)
{
    strncpy(mszMessage, message, sizeof(mszMessage) - 1);
    mszMessage[sizeof(mszMessage) - 1] = 0;
    mbMessageIsError = isError;
    if (Tui::IsActive() && Tui::IsLargeEnough())
    {
        DrawMessage();
        Tui::Flush();
    }
}

/// @brief Display background messages (ROM received...) in the message line instead of letting them break the screen
void DebugTuiView::PollAsyncMessages()
{
    char messages[512];
    if (!CommandManager::PopPendingMessages(messages, sizeof(messages)))
    {
        return;
    }
    // Keep the last non empty line
    char *last = nullptr;
    for (char *line = strtok(messages, "\n"); line != nullptr; line = strtok(nullptr, "\n"))
    {
        if (line[0] != 0)
        {
            last = line;
        }
    }
    if (last != nullptr)
    {
        SetMessage(last);
    }
}

void DebugTuiView::DrawStatic()
{
    Tui::ResetAttributes();
    Tui::SetForeground(COLOR_BORDER);
    Tui::Box(ROW_BOXES, COL_REGISTERS, HEIGHT_BOXES, mnWidthRegisters, "Registers");
    Tui::Box(ROW_BOXES, mnColStatus, HEIGHT_BOXES, mnWidthStatus, "Status");
    Tui::Box(ROW_CODE, 1, mnHeightCode, mnScreenWidth, "Program");

    // Fixed labels
    Tui::SetForeground(COLOR_LABEL);
    for (int i = 0; i < 8; ++i)
    {
        Tui::PrintAt(ROW_BOXES + 1 + i, COL_REGISTERS + 3, "D%d", i);
        Tui::PrintAt(ROW_BOXES + 1 + i, COL_REGISTERS + mnColAddressRegisters, "A%d", i);
    }
    Tui::PrintAt(ROW_BOXES + 1, mnColStatus + 3, "PC");
    Tui::PrintAt(ROW_BOXES + 2, mnColStatus + 3, "SR");
    Tui::PrintAt(ROW_BOXES + 4, mnColStatus + 3, "T  S  I2 I1 I0   X  N  Z  V  C");
    Tui::ResetAttributes();
}

void DebugTuiView::DrawTitle()
{
    Tui::SetColors(COLOR_BAR_FG, COLOR_BAR_BG);
    Tui::FillLine(ROW_TITLE, 1, mnScreenWidth);
    Tui::SetBold(true);
    Tui::PrintAt(ROW_TITLE, 2, "BricoNeo - 68000 live debugger");
    Tui::SetBold(false);

    if (mpFrame != nullptr)
    {
        Tui::PrintAt(ROW_TITLE, mnColStatus, "frame #%-5u  steps %-6lu", mpFrame->frameNumber, (unsigned long)mnStepCount);
    }

    const char *stateText;
    uint8_t stateColor;
    switch (meState)
    {
    case State::Stopped:
        stateText = " STOPPED ";
        stateColor = COLOR_STATE_STOPPED;
        break;
    case State::Running:
        stateText = " RUNNING ";
        stateColor = COLOR_STATE_RUNNING;
        break;
    default:
        stateText = " WAITING ";
        stateColor = COLOR_STATE_WAITING;
        break;
    }
    Tui::SetColors(COLOR_BAR_FG, stateColor);
    Tui::SetBold(true);
    Tui::PrintAt(ROW_TITLE, mnScreenWidth - 9, "%s", stateText);
    Tui::ResetAttributes();
}

static void PrintRegisterValue(bool known, bool changed, uint32_t value)
{
    if (!known)
    {
        Tui::SetForeground(COLOR_VALUE_UNKNOWN);
        Tui::Print("--------");
        return;
    }
    Tui::SetForeground(changed ? COLOR_VALUE_CHANGED : COLOR_VALUE);
    Tui::SetBold(changed);
    Tui::Print("%08lx", (unsigned long)value);
    Tui::SetBold(false);
}

void DebugTuiView::DrawRegisters()
{
    bool known = mpFrame != nullptr;
    for (int i = 0; i < 8; ++i)
    {
        Tui::MoveTo(ROW_BOXES + 1 + i, COL_REGISTERS + 7);
        PrintRegisterValue(known, known && mpPreviousFrame != nullptr && mpPreviousFrame->d[i] != mpFrame->d[i], known ? mpFrame->d[i] : 0);
        Tui::MoveTo(ROW_BOXES + 1 + i, COL_REGISTERS + mnColAddressRegisters + 4);
        PrintRegisterValue(known, known && mpPreviousFrame != nullptr && mpPreviousFrame->a[i] != mpFrame->a[i], known ? mpFrame->a[i] : 0);  // a[7] already holds the effective a7
    }
    Tui::ResetAttributes();
}

void DebugTuiView::DrawStatus()
{
    bool known = mpFrame != nullptr;
    bool hasPrevious = known && mpPreviousFrame != nullptr;

    // PC and SR
    Tui::MoveTo(ROW_BOXES + 1, mnColStatus + 7);
    PrintRegisterValue(known, false, known ? mpFrame->pc : 0);
    Tui::MoveTo(ROW_BOXES + 2, mnColStatus + 7);
    if (known)
    {
        bool changed = hasPrevious && mpPreviousFrame->sr != mpFrame->sr;
        Tui::SetForeground(changed ? COLOR_VALUE_CHANGED : COLOR_VALUE);
        Tui::SetBold(changed);
        Tui::Print("%04x    ", mpFrame->sr);
        Tui::SetBold(false);
    }
    else
    {
        Tui::SetForeground(COLOR_VALUE_UNKNOWN);
        Tui::Print("----    ");
    }

    // Flags, aligned under "T  S  I2 I1 I0   X  N  Z  V  C"
    static const struct { uint8_t bit; uint8_t column; } flags[] =
    {
        { 15, 0 }, { 13, 3 }, { 10, 6 }, { 9, 9 }, { 8, 12 },
        { 4, 17 }, { 3, 20 }, { 2, 23 }, { 1, 26 }, { 0, 29 },
    };
    for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
    {
        Tui::MoveTo(ROW_BOXES + 5, mnColStatus + 3 + flags[i].column);
        if (!known)
        {
            Tui::SetForeground(COLOR_VALUE_UNKNOWN);
            Tui::Print("-");
            continue;
        }
        uint16_t mask = 1 << flags[i].bit;
        bool on = (mpFrame->sr & mask) != 0;
        bool changed = hasPrevious && ((mpPreviousFrame->sr ^ mpFrame->sr) & mask) != 0;
        Tui::SetForeground(changed ? COLOR_VALUE_CHANGED : (on ? COLOR_FLAG_ON : COLOR_FLAG_OFF));
        Tui::SetBold(on || changed);
        Tui::Print("%d", on ? 1 : 0);
        Tui::SetBold(false);
    }

    // Human readable summary
    Tui::SetForeground(COLOR_VALUE);
    Tui::MoveTo(ROW_BOXES + 7, mnColStatus + 3);
    if (known)
    {
        Tui::Print("%-10s IRQ mask %d   Trace %-3s",
            (mpFrame->sr & (1 << 13)) ? "Supervisor" : "User",
            (mpFrame->sr >> 8) & 7,
            (mpFrame->sr & (1 << 15)) ? "on" : "off");
    }
    else
    {
        Tui::RepeatChar(' ', mnWidthStatus - 4);
    }
    Tui::ResetAttributes();
}

void DebugTuiView::DrawCode()
{
    const int innerWidth = mnScreenWidth - 2;
    // Margin, arrow and address on the left, up to DEBUG_INSTRUCTION_MAX_WORDS hex words on the right:
    // the instruction text takes the rest
    int textWidth = innerWidth - 13 - DEBUG_INSTRUCTION_MAX_WORDS * 5 - 1;
    if (textWidth < CODE_TEXT_MIN_WIDTH)
    {
        textWidth = CODE_TEXT_MIN_WIDTH;
    }
    if (textWidth > DEBUG_INSTRUCTION_MAX_TEXT - 1)
    {
        textWidth = DEBUG_INSTRUCTION_MAX_TEXT - 1;
    }

    // Static: too large for the stack
    static DisassembledInstruction instructions[CODE_MAX_LINES];
    int count = mpFrame != nullptr ? DisassembleFrame(mpFrame, instructions, mnCodeLines) : 0;

    for (int line = 0; line < mnCodeLines; ++line)
    {
        uint16_t row = ROW_CODE + 1 + line;
        bool current = line == 0;

        Tui::ResetAttributes();
        if (current)
        {
            Tui::SetBackground(COLOR_CURRENT_BG);
        }
        Tui::FillLine(row, 2, innerWidth);

        if (line >= count)
        {
            continue;
        }
        const DisassembledInstruction *instruction = &instructions[line];

        Tui::MoveTo(row, 3);
        Tui::SetForeground(COLOR_CURRENT_ARROW);
        Tui::SetBold(current);
        Tui::Print(current ? "->" : "  ");
        Tui::SetBold(false);
        Tui::SetForeground(COLOR_CODE_ADDRESS);
        Tui::Print(" %06lx  ", (unsigned long)(instruction->address & 0xffffff));
        Tui::SetForeground(COLOR_CODE_TEXT);
        Tui::SetBold(current);
        Tui::Print("%-*.*s", textWidth, textWidth, instruction->truncated ? "..." : instruction->text);
        Tui::SetBold(false);
        Tui::SetForeground(COLOR_CODE_HEX);
        for (int w = 0; w < instruction->wordCount; ++w)
        {
            Tui::Print(" %04x", instruction->words[w]);
        }
    }
    Tui::ResetAttributes();
}

void DebugTuiView::DrawMessage()
{
    Tui::ResetAttributes();
    Tui::FillLine(mnRowMessage, 1, mnScreenWidth);
    Tui::SetForeground(mbMessageIsError ? COLOR_ERROR : COLOR_MESSAGE);
    Tui::PrintAt(mnRowMessage, 2, "%.*s", mnScreenWidth - 2, mszMessage);
    Tui::ResetAttributes();
}

void DebugTuiView::DrawHelp()
{
    static const char *keys[][2] =
    {
        { "s/Space", "Step" },
        { "c/F5", "Continue" },
        { "t/F2", "Classic view" },
        { "r", "Redraw" },
        { "x/Esc", "Exit" },
    };
    Tui::SetColors(COLOR_BAR_FG, COLOR_BAR_BG);
    // Last line of the screen: writing its last cell may scroll some terminals. Fill that cell with "erase to end
    // of line" instead, which uses the current background color on most terminals.
    Tui::FillLine(mnRowHelp, 1, mnScreenWidth - 1);
    Tui::ClearToEndOfLine();
    Tui::MoveTo(mnRowHelp, 2);
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        Tui::SetForeground(COLOR_BAR_KEY);
        Tui::SetBold(true);
        Tui::Print("%s", keys[i][0]);
        Tui::SetBold(false);
        Tui::SetForeground(COLOR_BAR_FG);
        Tui::Print(" %s   ", keys[i][1]);
    }
    Tui::ResetAttributes();
}

void DebugTuiView::DrawTooSmall()
{
    Tui::ResetAttributes();
    Tui::SetForeground(COLOR_MESSAGE);
    Tui::PrintAt(1, 1, "Terminal too small (%ux%u)", Tui::GetCols(), Tui::GetRows());
    Tui::PrintAt(2, 1, "The debugger view needs at least %ux%u.", Tui::MIN_COLS, Tui::MIN_ROWS);
    Tui::PrintAt(4, 1, "Resize the window and press 'r',");
    Tui::PrintAt(5, 1, "or press 't' for the classic view.");
    Tui::ResetAttributes();
}
