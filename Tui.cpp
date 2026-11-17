#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "Tui.h"

// Box drawing style:
//  - TUI_LINE_DRAWING_UTF8: Unicode box drawing characters, UTF-8 encoded. Works with any UTF-8 terminal
//    (PuTTY default settings, Tera Term 5, Windows Terminal, xterm...).
//  - TUI_LINE_DRAWING_DEC: DEC Special Graphics character set (VT100). Note that PuTTY ignores it in UTF-8 mode,
//    unless "Window > Translation > Enable VT100 line drawing even in UTF-8 mode" is checked.
//  - TUI_LINE_DRAWING_ASCII: plain ASCII (+, -, |), works everywhere.
#define TUI_LINE_DRAWING_ASCII  0
#define TUI_LINE_DRAWING_DEC    1
#define TUI_LINE_DRAWING_UTF8   2

#ifndef TUI_LINE_DRAWING
#define TUI_LINE_DRAWING TUI_LINE_DRAWING_UTF8
#endif

#if TUI_LINE_DRAWING == TUI_LINE_DRAWING_UTF8
#define TUI_LINE_MODE_ON    ""
#define TUI_LINE_MODE_OFF   ""
#define TUI_CORNER_TL       "┌"
#define TUI_CORNER_TR       "┐"
#define TUI_CORNER_BL       "└"
#define TUI_CORNER_BR       "┘"
#define TUI_LINE_H          "─"
#define TUI_LINE_V          "│"
#elif TUI_LINE_DRAWING == TUI_LINE_DRAWING_DEC
#define TUI_LINE_MODE_ON    "\e(0"
#define TUI_LINE_MODE_OFF   "\e(B"
#define TUI_CORNER_TL       "l"
#define TUI_CORNER_TR       "k"
#define TUI_CORNER_BL       "m"
#define TUI_CORNER_BR       "j"
#define TUI_LINE_H          "q"
#define TUI_LINE_V          "x"
#else
#define TUI_LINE_MODE_ON    ""
#define TUI_LINE_MODE_OFF   ""
#define TUI_CORNER_TL       "+"
#define TUI_CORNER_TR       "+"
#define TUI_CORNER_BL       "+"
#define TUI_CORNER_BR       "+"
#define TUI_LINE_H          "-"
#define TUI_LINE_V          "|"
#endif

/// @brief Output a string without any formatting
static void PutRawString(const char *s)
{
    while (*s)
    {
        putchar_raw(*s++);
    }
}

/// @brief Output a (possibly multi-byte) string several times
static void RepeatRawString(const char *s, int count)
{
    for (int i = 0; i < count; ++i)
    {
        PutRawString(s);
    }
}

// Time to wait for the rest of an escape sequence after an ESC character
#define TUI_ESCAPE_SEQUENCE_TIMEOUT_US  10000
// Time to wait for the terminal to answer the cursor position request
#define TUI_SIZE_QUERY_TIMEOUT_US       200000
// Period of the terminal size polling while the TUI is active
#define TUI_SIZE_POLL_PERIOD_US         500000

// Save cursor, move it as far as possible to the bottom right, ask for its position (DSR), restore cursor.
// The answer (ESC [ rows ; cols R) gives the terminal size.
#define TUI_SIZE_QUERY_SEQUENCE         "\e7\e[999;999H\e[6n\e8"

bool Tui::gbActive = false;
uint16_t Tui::gnRows = Tui::MIN_ROWS;
uint16_t Tui::gnCols = Tui::MIN_COLS;
bool Tui::gbSavedCursorValid = false;
uint16_t Tui::gnSavedCursorRow = 0;
uint16_t Tui::gnSavedCursorCol = 0;
bool Tui::gbAlternateScreenMissing = false;
bool Tui::gbSizeRequestPending = false;
uint64_t Tui::gnSizeRequestTimeUs = 0;

/// @brief Switch to the alternate screen, hide the cursor and query the terminal size
void Tui::Enter()
{
    if (gbActive)
    {
        return;
    }
    gbActive = true;
    // Remember where the cursor is in the classic console: Leave() checks it is restored there
    gbSavedCursorValid = QueryCursorPosition(&gnSavedCursorRow, &gnSavedCursorCol);
    // Alternate screen buffer, hidden cursor, default attributes, cleared screen
    printf("\e[?1049h\e[?25l\e[0m\e[2J\e[H");
    QuerySize();
    gnSizeRequestTimeUs = time_us_64();
}

/// @brief Go back to the classic console, as it was before Enter()
void Tui::Leave()
{
    if (!gbActive)
    {
        return;
    }
    gbActive = false;
    // Do not let a late size answer reach the classic console
    WaitPendingSizeAnswer();
    printf("\e[0m" TUI_LINE_MODE_OFF);

    // Put the cursor somewhere it cannot have been in the classic console
    if (gbSavedCursorValid)
    {
        MoveTo(gnSavedCursorRow == 1 ? 2 : 1, 1);
    }

    // Back to the main screen: the terminal restores its content and the cursor position
    printf("\e[?25h\e[?1049l");
    Flush();

    // Terminals without alternate screen (e.g. GNU screen without "altscreen on") ignore it: the full screen view
    // stays displayed and the cursor is left where it was put above. In that case, at least continue below the view.
    uint16_t row, col;
    if (gbSavedCursorValid && QueryCursorPosition(&row, &col))
    {
        gbAlternateScreenMissing = row != gnSavedCursorRow || col != gnSavedCursorCol;
        if (gbAlternateScreenMissing)
        {
            printf("\e[%u;1H\r\n", gnRows);
            Flush();
        }
    }
}

/// @brief Ask the terminal for its size (blocking). Falls back to 80x24 if the terminal does not answer.
/// While the TUI is active, ReadKey() also polls the size in the background and reports changes with Key::Resize.
/// @return true if the terminal answered
bool Tui::QuerySize()
{
    WaitPendingSizeAnswer();
    // Save cursor, move it as far as possible to the bottom right, ask for its position, restore cursor
    printf("\e7\e[999;999H");
    uint16_t rows, cols;
    bool answered = QueryCursorPosition(&rows, &cols);
    printf("\e8");
    Flush();
    gnRows = answered ? rows : MIN_ROWS;
    gnCols = answered ? cols : MIN_COLS;
    return answered;
}

/// @brief Send a size query without waiting for the answer: it is decoded by ReadKey() as any other escape sequence
void Tui::RequestSizeAsync()
{
    printf(TUI_SIZE_QUERY_SEQUENCE);
    Flush();
    gbSizeRequestPending = true;
    gnSizeRequestTimeUs = time_us_64();
}

/// @brief If an asynchronous size query is in progress, consume its answer (so that it is not mistaken for another
/// answer, nor printed on the classic console). Input received meanwhile is lost.
void Tui::WaitPendingSizeAnswer()
{
    if (!gbSizeRequestPending)
    {
        return;
    }
    gbSizeRequestPending = false;
    absolute_time_t timeout = make_timeout_time_us(TUI_SIZE_QUERY_TIMEOUT_US);
    while (!time_reached(timeout))
    {
        if (ReadRawChar(1000) == 'R')
        {
            return;
        }
    }
}

/// @brief Ask the terminal for the cursor position (Device Status Report)
/// @return true if the terminal answered
bool Tui::QueryCursorPosition(uint16_t *row, uint16_t *col)
{
    // Flush pending input so that keystrokes are not mixed with the answer
    while (ReadRawChar(0) >= 0);

    printf("\e[6n");
    Flush();

    // Expected answer: ESC [ row ; col R
    char answer[16];
    int length = 0;
    absolute_time_t timeout = make_timeout_time_us(TUI_SIZE_QUERY_TIMEOUT_US);
    while (!time_reached(timeout))
    {
        int c = ReadRawChar(1000);
        if (c < 0)
        {
            continue;
        }
        if (c == 0x1b)
        {
            length = 0;
        }
        if (length < (int)sizeof(answer) - 1)
        {
            answer[length++] = (char)c;
        }
        if (c == 'R')
        {
            answer[length] = 0;
            int r, c2;
            if (sscanf(answer, "\e[%d;%dR", &r, &c2) == 2 && r > 0 && c2 > 0)
            {
                *row = (uint16_t)r;
                *col = (uint16_t)c2;
                return true;
            }
            return false;
        }
    }
    return false;
}

void Tui::Clear()
{
    printf("\e[0m\e[2J\e[H");
}

void Tui::MoveTo(uint16_t row, uint16_t col)
{
    printf("\e[%u;%uH", row, col);
}

void Tui::ClearToEndOfLine()
{
    printf("\e[K");
}

void Tui::ResetAttributes()
{
    printf("\e[0m");
}

void Tui::SetForeground(uint8_t color)
{
    printf("\e[38;5;%um", color);
}

void Tui::SetBackground(uint8_t color)
{
    printf("\e[48;5;%um", color);
}

void Tui::SetColors(uint8_t foreground, uint8_t background)
{
    printf("\e[38;5;%u;48;5;%um", foreground, background);
}

void Tui::SetBold(bool bold)
{
    printf(bold ? "\e[1m" : "\e[22m");
}

void Tui::SetReverse(bool reverse)
{
    printf(reverse ? "\e[7m" : "\e[27m");
}

void Tui::Print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void Tui::PrintAt(uint16_t row, uint16_t col, const char *format, ...)
{
    MoveTo(row, col);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void Tui::RepeatChar(char c, int count)
{
    for (int i = 0; i < count; ++i)
    {
        putchar_raw(c);
    }
}

/// @brief Fill a line segment with a character using the current attributes (useful for colored bars)
void Tui::FillLine(uint16_t row, uint16_t col, int width, char c)
{
    MoveTo(row, col);
    RepeatChar(c, width);
}

/// @brief Draw a frame, with an optional title embedded in the top border. The inside is not cleared.
void Tui::Box(uint16_t row, uint16_t col, uint16_t height, uint16_t width, const char *title)
{
    if (height < 2 || width < 2)
    {
        return;
    }

    // Top border
    MoveTo(row, col);
    PutRawString(TUI_LINE_MODE_ON TUI_CORNER_TL);
    int topRemaining = width - 2;
    if (title != nullptr && title[0] != 0 && width > 6)
    {
        int titleLength = strlen(title);
        if (titleLength > width - 6)
        {
            titleLength = width - 6;
        }
        PutRawString(TUI_LINE_H);
        printf(TUI_LINE_MODE_OFF " %.*s " TUI_LINE_MODE_ON, titleLength, title);
        topRemaining -= titleLength + 3;
    }
    RepeatRawString(TUI_LINE_H, topRemaining);
    PutRawString(TUI_CORNER_TR);

    // Sides
    for (uint16_t i = 1; i < height - 1; ++i)
    {
        MoveTo(row + i, col);
        PutRawString(TUI_LINE_V);
        MoveTo(row + i, col + width - 1);
        PutRawString(TUI_LINE_V);
    }

    // Bottom border
    MoveTo(row + height - 1, col);
    PutRawString(TUI_CORNER_BL);
    RepeatRawString(TUI_LINE_H, width - 2);
    PutRawString(TUI_CORNER_BR TUI_LINE_MODE_OFF);
}

void Tui::Flush()
{
    stdio_flush();
}

int Tui::ReadRawChar(uint32_t timeoutUs)
{
    return getchar_timeout_us(timeoutUs);
}

/// @brief Non blocking key read
/// @return true if a key has been read
bool Tui::ReadKey(KeyEvent *event)
{
    // Background terminal size polling
    if (gbActive)
    {
        uint64_t elapsedUs = time_us_64() - gnSizeRequestTimeUs;
        if (gbSizeRequestPending && elapsedUs > TUI_SIZE_QUERY_TIMEOUT_US)
        {
            // No answer: the terminal does not support it, try again later
            gbSizeRequestPending = false;
        }
        if (!gbSizeRequestPending && elapsedUs > TUI_SIZE_POLL_PERIOD_US)
        {
            RequestSizeAsync();
        }
    }

    int c = ReadRawChar(10);
    if (c < 0)
    {
        return false;
    }

    event->c = (char)c;
    switch (c)
    {
    case 0x1b:
        event->key = DecodeEscapeSequence();
        break;
    case '\r':
    case '\n':
        event->key = Key::Enter;
        break;
    case 0x7f:
    case 0x08:
        event->key = Key::Backspace;
        break;
    case '\t':
        event->key = Key::Tab;
        break;
    default:
        event->key = Key::Char;
        break;
    }
    return true;
}

/// @brief Called after an ESC character has been received: decode the CSI (ESC [) or SS3 (ESC O) sequence that may follow
Tui::Key Tui::DecodeEscapeSequence()
{
    int introducer = ReadRawChar(TUI_ESCAPE_SEQUENCE_TIMEOUT_US);
    if (introducer < 0)
    {
        // Nothing follows: the Escape key itself
        return Key::Escape;
    }

    if (introducer == 'O')
    {
        // SS3 sequences: F1-F4 and some cursor keys (application mode)
        switch (ReadRawChar(TUI_ESCAPE_SEQUENCE_TIMEOUT_US))
        {
        case 'P': return Key::F1;
        case 'Q': return Key::F2;
        case 'R': return Key::F3;
        case 'S': return Key::F4;
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return Key::Right;
        case 'D': return Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        default:  return Key::None;
        }
    }

    if (introducer != '[')
    {
        // Unknown (Alt+key...): ignore
        return Key::None;
    }

    // CSI sequence: optional numeric parameters separated by ';', then a final character.
    // For keys, only the first parameter matters (the second one is the modifier).
    int parameters[2] = { 0, 0 };
    int parameterIndex = 0;
    int parameterCount = 0;
    while (true)
    {
        int c = ReadRawChar(TUI_ESCAPE_SEQUENCE_TIMEOUT_US);
        if (c < 0)
        {
            return Key::None;
        }
        if (c >= '0' && c <= '9')
        {
            if (parameterIndex < 2)
            {
                parameters[parameterIndex] = parameters[parameterIndex] * 10 + (c - '0');
                if (parameterCount <= parameterIndex)
                {
                    parameterCount = parameterIndex + 1;
                }
            }
            continue;
        }
        if (c == ';')
        {
            ++parameterIndex;
            continue;
        }
        int parameter = parameters[0];
        switch (c)
        {
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return Key::Right;
        case 'D': return Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        case 'P': return Key::F1;
        case 'Q': return Key::F2;
        case 'R':
            // Size query answer (ESC [ rows ; cols R), or F3 with a modifier (ESC [ 1 ; modifier R)
            if (parameterCount == 2 && parameters[0] > 1)
            {
                gbSizeRequestPending = false;
                if (parameters[0] == gnRows && parameters[1] == gnCols)
                {
                    return Key::None;
                }
                gnRows = (uint16_t)parameters[0];
                gnCols = (uint16_t)parameters[1];
                return Key::Resize;
            }
            return Key::F3;
        case 'S': return Key::F4;
        case '~':
            switch (parameter)
            {
            case 1: case 7: return Key::Home;
            case 4: case 8: return Key::End;
            case 2:  return Key::Insert;
            case 3:  return Key::Delete;
            case 5:  return Key::PageUp;
            case 6:  return Key::PageDown;
            case 11: return Key::F1;
            case 12: return Key::F2;
            case 13: return Key::F3;
            case 14: return Key::F4;
            case 15: return Key::F5;
            case 17: return Key::F6;
            case 18: return Key::F7;
            case 19: return Key::F8;
            case 20: return Key::F9;
            case 21: return Key::F10;
            case 23: return Key::F11;
            case 24: return Key::F12;
            default: return Key::None;
            }
        default:
            return Key::None;
        }
    }
}
