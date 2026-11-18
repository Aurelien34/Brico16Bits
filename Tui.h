#pragma once

#include <cstdint>
#include <cstddef>

/// @brief Minimal full-screen text UI layer on top of the ANSI/VT100 serial console.
///
/// Uses the terminal "alternate screen" (as vim / less do): entering the TUI switches to a blank screen,
/// leaving it restores the classic console exactly as it was, scrollback included.
/// Rows and columns are 1-based, as in ANSI escape sequences.
class Tui
{
public:
    enum class Key : uint8_t
    {
        None,
        Char,           // Printable character, see KeyEvent::c
        Enter,
        Escape,
        Backspace,
        Tab,
        Up, Down, Left, Right,
        Home, End, PageUp, PageDown, Insert, Delete,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Resize,         // The terminal size changed (see GetRows() / GetCols()): the screen must be redrawn
    };

    struct KeyEvent
    {
        Key key;
        char c;         // Raw character for Key::Char (and control keys)
    };

    // Layout reference size: the TUI views are designed for at least 80x24
    static constexpr uint16_t MIN_ROWS = 24;
    static constexpr uint16_t MIN_COLS = 80;

    // Screen management
    static void Enter();
    static void Leave();
    static inline bool IsActive() { return gbActive; }
    static bool QuerySize();
    static inline uint16_t GetRows() { return gnRows; }
    static inline uint16_t GetCols() { return gnCols; }
    static inline bool IsLargeEnough() { return gnRows >= MIN_ROWS && gnCols >= MIN_COLS; }
    static inline bool IsAlternateScreenMissing() { return gbAlternateScreenMissing; }

    // Drawing
    static void Clear();
    static void MoveTo(uint16_t row, uint16_t col);
    static void ClearToEndOfLine();
    static void ResetAttributes();
    static void SetForeground(uint8_t color);
    static void SetBackground(uint8_t color);
    static void SetColors(uint8_t foreground, uint8_t background);
    static void SetBold(bool bold);
    static void SetReverse(bool reverse);
    static void Print(const char *format, ...) __attribute__((format(printf, 1, 2)));
    static void PrintAt(uint16_t row, uint16_t col, const char *format, ...) __attribute__((format(printf, 3, 4)));
    static void RepeatChar(char c, int count);
    static void FillLine(uint16_t row, uint16_t col, int width, char c = ' ');
    static void Box(uint16_t row, uint16_t col, uint16_t height, uint16_t width, const char *title = nullptr);
    static void Flush();

    // Keyboard: non blocking, decodes ANSI escape sequences (arrows, function keys...).
    // While the TUI is active, it also polls the terminal size and returns Key::Resize when it changes.
    static bool ReadKey(KeyEvent *event);

    /// @brief Blocking line editor (printable characters, Backspace, Enter, Escape). Works in the TUI (the field is
    /// redrawn at row/col with the current attributes) and in the classic console (echo, row/col ignored).
    /// Other keys (arrows, F-keys, Resize...) are ignored.
    /// @param width Width of the field in the TUI, the text is limited to width-1 characters (and size-1)
    /// @param buffer Receives the NUL-terminated text (emptied if Escape)
    /// @param idle Called regularly while waiting for a key (background tasks), may be nullptr
    /// @return true if validated with Enter, false if cancelled with Escape
    static bool ReadLine(uint16_t row, uint16_t col, uint16_t width, char *buffer, size_t size, void (*idle)() = nullptr);

private:
    static Key DecodeEscapeSequence();
    static void RequestSizeAsync();
    static void WaitPendingSizeAnswer();
    static int ReadRawChar(uint32_t timeoutUs);
    static bool QueryCursorPosition(uint16_t *row, uint16_t *col);

    // Classic console cursor position when entering the TUI, used to detect terminals without alternate screen
    static bool gbSavedCursorValid;
    static uint16_t gnSavedCursorRow;
    static uint16_t gnSavedCursorCol;
    static bool gbAlternateScreenMissing;

    // Asynchronous terminal size polling (there is no resize notification on a serial line)
    static bool gbSizeRequestPending;
    static uint64_t gnSizeRequestTimeUs;

    static bool gbActive;
    static uint16_t gnRows;
    static uint16_t gnCols;
};
