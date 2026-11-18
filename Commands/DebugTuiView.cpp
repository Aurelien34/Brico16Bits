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
#define MEMORY_MAX_LINES    (MEMORY_DUMP_MAX_BYTES / 16)
#define MEMORY_BYTES_PER_LINE 16
#define BOTH_PANELS_MIN_ROWS 32                         // From this height, code and memory are displayed together

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
#define COLOR_MEMORY_ASCII  250
#define TARGET_COLUMN       24      // Column of the branch target "($xxxxxx)" in the instruction text field
#define COLOR_TARGET        179     // Branch target
#define COLOR_TARGET_HISTORY 95     // Branch target on a history line
#define COLOR_HISTORY       242     // Executed instructions above the current one (dimmed)
#define HISTORY_MIN_LISTING 3       // Lines of the listing (current instruction included) kept when history takes lines
#define COLOR_CURRENT_BG    237
#define COLOR_CURRENT_ARROW 46
#define COLOR_MESSAGE       214
#define COLOR_ERROR         196
#define COLOR_STATE_WAITING 130
#define COLOR_STATE_STOPPED 124
#define COLOR_STATE_RUNNING 28

bool ExecutedInstruction::IsSizeReliable() const
{
    return !instruction.truncated && instruction.wordCount > 0 && strcmp(instruction.text, "Unknown") != 0;
}

bool ExecutedInstruction::IsContiguousTo(uint32_t nextPc) const
{
    if (forceBreak || !IsSizeReliable())
    {
        return false;
    }
    return ((instruction.address + GetSizeBytes()) & 0xffffff) == (nextPc & 0xffffff);
}

void ExecutionHistory::Push(const DisassembledInstruction &instruction, uint32_t sp, bool forceBreak)
{
    if (mnCount == MAX_ENTRIES)
    {
        for (int i = 1; i < MAX_ENTRIES; ++i)
        {
            maEntries[i - 1] = maEntries[i];
        }
        --mnCount;
    }
    ExecutedInstruction &entry = maEntries[mnCount++];
    entry.instruction = instruction;
    entry.sp = sp;
    entry.forceBreak = forceBreak;
}

int ExecutionHistory::BuildRows(uint32_t currentPc, HistoryRow *rows) const
{
    if (mnCount == 0)
    {
        return 0;
    }
    const ExecutedInstruction &latest = maEntries[mnCount - 1];
    if (!latest.IsContiguousTo(currentPc))
    {
        // Flow break: only say where we come from, the older entries are not at the addresses above the listing
        rows[0].pInstruction = &latest;
        rows[0].isFromLine = true;
        return 1;
    }
    int count = 0;
    if (mnCount == 2 && maEntries[0].IsContiguousTo(latest.instruction.address))
    {
        rows[count].pInstruction = &maEntries[0];
        rows[count++].isFromLine = false;
    }
    rows[count].pInstruction = &latest;
    rows[count++].isFromLine = false;
    return count;
}

/// @brief Print the instruction text then, if it is a relative branch, its absolute target "($xxxxxx)" at a fixed
/// column of the text field (right after the text if that one is longer), truncated to the field width.
/// The text field is padded to textWidth. Colors are set here; the caller restores its own afterwards.
static void PrintTextAndTarget(const DisassembledInstruction *instruction, int textWidth, uint8_t textColor, uint8_t targetColor, bool bold)
{
    const char *text = instruction->truncated ? "..." : instruction->text;
    int length = (int)strlen(text);
    if (length > textWidth)
    {
        length = textWidth;
    }
    Tui::SetForeground(textColor);
    Tui::SetBold(bold);
    Tui::Print("%.*s", length, text);
    Tui::SetBold(false);
    int position = length;
    if (instruction->hasTarget)
    {
        int column = length + 1 > TARGET_COLUMN ? length + 1 : TARGET_COLUMN;
        int room = textWidth - column;
        if (room > 0)
        {
            char target[12];
            snprintf(target, sizeof(target), "($%06lx)", (unsigned long)(instruction->target & 0xffffff));
            Tui::RepeatChar(' ', column - position);
            Tui::SetForeground(targetColor);
            Tui::Print("%.*s", room, target);
            position = column + (room < 9 ? room : 9);
        }
    }
    if (position < textWidth)
    {
        Tui::RepeatChar(' ', textWidth - position);
    }
}

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

    // Message and help lines at the bottom, program and/or memory boxes in between.
    // Below BOTH_PANELS_MIN_ROWS lines, only one of them is displayed (F3 switches); otherwise they share the height.
    mnRowHelp = mnScreenHeight;
    mnRowMessage = mnScreenHeight - 1;
    int available = (int)mnRowMessage - ROW_CODE;
    if (available < 0)
    {
        available = 0;
    }
    mbBothPanels = mnScreenHeight >= BOTH_PANELS_MIN_ROWS;
    // The memory panel only exists while the monitor is enabled (an address has been chosen)
    mbShowMemory = mbHasMemoryAddress && (mbBothPanels || mbMemorySelected);
    mbShowCode = !mbShowMemory || mbBothPanels;
    int heightMemory = 0;
    int heightCode = 0;
    if (mbShowMemory && mbShowCode)
    {
        heightMemory = available / 2;
        heightCode = available - heightMemory;
    }
    else if (mbShowMemory)
    {
        heightMemory = available;
    }
    else
    {
        heightCode = available;
    }
    mnRowCode = ROW_CODE;
    mnHeightCode = heightCode;
    mnRowMemory = ROW_CODE + heightCode;
    mnHeightMemory = heightMemory;
    mnCodeLines = heightCode >= 2 ? heightCode - 2 : 0;
    if (mnCodeLines > CODE_MAX_LINES)
    {
        mnCodeLines = CODE_MAX_LINES;
    }
    mnMemoryLines = heightMemory >= 2 ? heightMemory - 2 : 0;
    if (mnMemoryLines > MEMORY_MAX_LINES)
    {
        mnMemoryLines = MEMORY_MAX_LINES;
    }
}

bool DebugTuiView::ToggleMemoryPanel()
{
    ComputeLayout();
    if (mbBothPanels || !mbHasMemoryAddress)
    {
        return false;
    }
    mbMemorySelected = !mbMemorySelected;
    Redraw(false);
    return true;
}

/// @brief Update the memory panel (see header)
/// @brief Enable / disable the memory monitor. Enabling shows the memory panel (instead of the code on a small
/// terminal), disabling gives the whole height back to the code panel. Redraws the screen if the state changes.
void DebugTuiView::SetMemoryEnabled(bool enabled)
{
    if (enabled == mbHasMemoryAddress)
    {
        return;
    }
    mbHasMemoryAddress = enabled;
    mbMemorySelected = enabled;
    if (Tui::IsActive())
    {
        Redraw(false);
    }
}

void DebugTuiView::SetMemoryWordMode(bool words)
{
    mbMemoryWords = words;
    if (Tui::IsActive() && Tui::IsLargeEnough())
    {
        DrawMemory();
        Tui::Flush();
    }
}

void DebugTuiView::ShowMemory(const MemoryDump *dump, bool hasAddress, uint32_t address, const char *error)
{
    SetMemoryEnabled(hasAddress);
    mpMemory = dump;
    mnMemoryAddress = address;
    strncpy(mszMemoryError, error != nullptr ? error : "", sizeof(mszMemoryError) - 1);
    mszMemoryError[sizeof(mszMemoryError) - 1] = 0;
    if (Tui::IsActive() && Tui::IsLargeEnough())
    {
        DrawMemory();
        Tui::Flush();
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
        DrawMemory();
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

/// @brief Prompt in the message line and read a line. The message line is cleared afterwards.
/// @return true if validated with Enter, false if cancelled with Escape
bool DebugTuiView::PromptInput(const char *prompt, char *buffer, size_t size, void (*idle)())
{
    Tui::ResetAttributes();
    Tui::FillLine(mnRowMessage, 1, mnScreenWidth);
    Tui::SetForeground(COLOR_MESSAGE);
    Tui::PrintAt(mnRowMessage, 2, "%.*s", mnScreenWidth - 4, prompt);
    int column = 2 + (int)strlen(prompt);
    int width = (int)mnScreenWidth - column;
    if (width < 4)
    {
        column = 2;
        width = (int)mnScreenWidth - 2;
    }
    Tui::SetForeground(COLOR_VALUE);
    bool validated = Tui::ReadLine(mnRowMessage, column, width, buffer, size, idle);
    Tui::ResetAttributes();
    mszMessage[0] = 0;
    mbMessageIsError = false;
    DrawMessage();
    Tui::Flush();
    return validated;
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
    if (mbShowCode)
    {
        Tui::Box(mnRowCode, 1, mnHeightCode, mnScreenWidth, "Program");
    }
    if (mbShowMemory)
    {
        Tui::Box(mnRowMemory, 1, mnHeightMemory, mnScreenWidth, "Memory");
    }

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
    if (!mbShowCode)
    {
        return;
    }
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

    // History lines (executed instructions, or a "from" line) are drawn above the current instruction and take
    // lines from the end of the listing. The oldest ones are dropped first, keeping HISTORY_MIN_LISTING lines.
    HistoryRow historyRows[ExecutionHistory::MAX_ROWS];
    int historyCount = 0;
    const HistoryRow *pHistory = historyRows;
    if (mpHistory != nullptr && mpFrame != nullptr)
    {
        historyCount = mpHistory->BuildRows(mpFrame->pc, historyRows);
        int maxHistory = mnCodeLines - HISTORY_MIN_LISTING;
        if (maxHistory < 0)
        {
            maxHistory = 0;
        }
        if (historyCount > maxHistory)
        {
            int skip = historyCount - maxHistory;
            pHistory += skip;
            historyCount -= skip;
        }
    }

    // Static: too large for the stack
    static DisassembledInstruction instructions[CODE_MAX_LINES];
    int count = mpFrame != nullptr ? DisassembleFrame(mpFrame, instructions, mnCodeLines - historyCount) : 0;

    for (int line = 0; line < mnCodeLines; ++line)
    {
        uint16_t row = mnRowCode + 1 + line;
        bool isHistory = line < historyCount;
        int index = line - historyCount;    // Index in the listing
        bool current = index == 0;

        Tui::ResetAttributes();
        if (current)
        {
            Tui::SetBackground(COLOR_CURRENT_BG);
        }
        Tui::FillLine(row, 2, innerWidth);

        if (isHistory)
        {
            Tui::MoveTo(row, 3);
            Tui::SetForeground(COLOR_HISTORY);
            const HistoryRow &historyRow = pHistory[line];
            const DisassembledInstruction *past = &historyRow.pInstruction->instruction;
            Tui::Print("%s", historyRow.isFromLine ? "<- " : "   ");
            Tui::Print("%06lx  ", (unsigned long)(past->address & 0xffffff));
            PrintTextAndTarget(past, textWidth, COLOR_HISTORY, COLOR_TARGET_HISTORY, false);
            Tui::SetForeground(COLOR_HISTORY);
            for (int w = 0; w < past->wordCount; ++w)
            {
                Tui::Print(" %04x", past->words[w]);
            }
            continue;
        }
        if (index >= count)
        {
            continue;
        }
        const DisassembledInstruction *instruction = &instructions[index];

        Tui::MoveTo(row, 3);
        Tui::SetForeground(COLOR_CURRENT_ARROW);
        Tui::SetBold(current);
        Tui::Print(current ? "->" : "  ");
        Tui::SetBold(false);
        Tui::SetForeground(COLOR_CODE_ADDRESS);
        Tui::Print(" %06lx  ", (unsigned long)(instruction->address & 0xffffff));
        PrintTextAndTarget(instruction, textWidth, COLOR_CODE_TEXT, COLOR_TARGET, current);
        Tui::SetForeground(COLOR_CODE_HEX);
        for (int w = 0; w < instruction->wordCount; ++w)
        {
            Tui::Print(" %04x", instruction->words[w]);
        }
    }
    Tui::ResetAttributes();
}

/// @brief Hexadecimal + ASCII dump, 16 bytes per line. Bytes changed since the previous dump are highlighted
/// in both columns; bytes not covered by the current dump are shown as "--".
void DebugTuiView::DrawMemory()
{
    if (!mbShowMemory)
    {
        return;
    }
    const int innerWidth = mnScreenWidth - 2;
    for (int line = 0; line < mnMemoryLines; ++line)
    {
        uint16_t row = mnRowMemory + 1 + line;
        Tui::ResetAttributes();
        Tui::FillLine(row, 2, innerWidth);

        if (!mbHasMemoryAddress || mszMemoryError[0] != 0)
        {
            if (line == 0)
            {
                Tui::MoveTo(row, 3);
                bool error = mszMemoryError[0] != 0;
                Tui::SetForeground(error ? COLOR_ERROR : COLOR_VALUE_UNKNOWN);
                Tui::Print("%.*s", innerWidth - 2, error ? mszMemoryError : "No address: press 'm' to enter one");
            }
            continue;
        }

        uint32_t lineAddress = mnMemoryAddress + (uint32_t)line * MEMORY_BYTES_PER_LINE;
        Tui::MoveTo(row, 3);
        Tui::SetForeground(COLOR_CODE_ADDRESS);
        Tui::Print("%06lx  ", (unsigned long)(lineAddress & 0xffffff));

        uint8_t values[MEMORY_BYTES_PER_LINE];
        uint8_t known[MEMORY_BYTES_PER_LINE];
        uint8_t changed[MEMORY_BYTES_PER_LINE];
        for (int i = 0; i < MEMORY_BYTES_PER_LINE; ++i)
        {
            uint32_t address = lineAddress + i;
            known[i] = mpMemory != nullptr && mpMemory->GetByte(address, &values[i]);
            changed[i] = known[i] && mpMemory->IsByteChanged(address);
        }

        for (int i = 0; i < MEMORY_BYTES_PER_LINE; ++i)
        {
            Tui::SetForeground(!known[i] ? COLOR_VALUE_UNKNOWN : (changed[i] ? COLOR_VALUE_CHANGED : COLOR_VALUE));
            Tui::SetBold(changed[i]);
            if (known[i])
            {
                Tui::Print("%02x", values[i]);
            }
            else
            {
                Tui::Print("--");
            }
            Tui::SetBold(false);
            // 8-bit mode: a space after each byte; 16-bit mode: after each word (each byte keeps its own highlighting)
            if (!mbMemoryWords || (i & 1))
            {
                Tui::Print(i == 7 ? "  " : " ");
            }
        }

        Tui::Print(" ");
        for (int i = 0; i < MEMORY_BYTES_PER_LINE; ++i)
        {
            Tui::SetForeground(!known[i] ? COLOR_VALUE_UNKNOWN : (changed[i] ? COLOR_VALUE_CHANGED : COLOR_MEMORY_ASCII));
            Tui::SetBold(changed[i]);
            Tui::Print("%c", (known[i] && values[i] >= 0x20 && values[i] < 0x7f) ? (char)values[i] : '.');
            Tui::SetBold(false);
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
        { "s", "Step" },
        { "c", "Cont" },
        { "m", "Mem" },
        { "w", "8/16" },
        { "PgUp/Dn", "Scroll" },
        { "F3", "Code/Mem" },
        { "t", "View" },
        { "x", "Exit" },
    };
    Tui::SetColors(COLOR_BAR_FG, COLOR_BAR_BG);
    // Last line of the screen: writing its last cell may scroll some terminals. Fill that cell with "erase to end
    // of line" instead, which uses the current background color on most terminals.
    Tui::FillLine(mnRowHelp, 1, mnScreenWidth - 1);
    Tui::ClearToEndOfLine();
    Tui::MoveTo(mnRowHelp, 2);
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        bool memoryKey = strcmp(keys[i][0], "w") == 0 || strcmp(keys[i][0], "PgUp/Dn") == 0;
        if ((memoryKey && !mbHasMemoryAddress) || (strcmp(keys[i][0], "F3") == 0 && (mbBothPanels || !mbHasMemoryAddress)))
        {
            continue;   // Monitor disabled, or both panels displayed (nothing to toggle)
        }
        Tui::SetForeground(COLOR_BAR_KEY);
        Tui::SetBold(true);
        Tui::Print("%s", keys[i][0]);
        Tui::SetBold(false);
        Tui::SetForeground(COLOR_BAR_FG);
        Tui::Print(" %s  ", keys[i][1]);
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
