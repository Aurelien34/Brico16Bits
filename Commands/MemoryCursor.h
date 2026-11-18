#pragma once

#include <cstdint>

/// @brief Cursor of the memory monitor edit mode (pure logic, no display): one cell (byte or 16-bit word) in a zone of
/// BYTES_PER_LINE bytes per line, plus the hexadecimal digits typed so far (pending value).
/// The zone address is owned by the caller and passed by pointer: moving the cursor past the first / last displayed
/// line scrolls the zone by one line. All move functions return true if the zone address changed (memory to re-read).
class MemoryCursor
{
public:
    static const uint32_t BYTES_PER_LINE = 16;
    static const uint32_t ADDRESS_SPACE = 0x1000000u;   ///< 68000: 24 bits address bus

    /// @brief Cursor on the first cell of the zone, no pending value
    void Reset(uint32_t zoneAddress, bool words);

    uint32_t GetAddress() const { return mnAddress; }
    bool IsWordMode() const { return mbWords; }
    uint32_t GetCellSize() const { return mbWords ? 2 : 1; }
    /// @brief true if the byte address belongs to the cell under the cursor
    bool Contains(uint32_t byteAddress) const { return byteAddress >= mnAddress && byteAddress < mnAddress + GetCellSize(); }

    /// @brief Byte / word mode. The address is kept (aligned down to even in word mode), the pending value is dropped.
    void SetWordMode(bool words);

    /// @brief Move by count cells (negative: backwards), clamped to the address space. Drops the pending value.
    bool MoveCells(int count, uint32_t *zoneAddress, int lines);
    /// @brief Move by count lines. Does nothing if it would leave the address space. Drops the pending value.
    bool MoveLines(int count, uint32_t *zoneAddress, int lines);
    /// @brief Move by one page (direction -1 / +1): the zone and the cursor move by "lines" lines; at the end of the
    /// address space the cursor goes to the first / last displayed line (same column). Drops the pending value.
    bool MovePage(int direction, uint32_t *zoneAddress, int lines);
    /// @brief Normalize the zone (aligned, inside the address space) and scroll it the minimum so that the cursor is
    /// displayed (layout change: resize...)
    bool EnsureVisible(uint32_t *zoneAddress, int lines);

    // Pending value
    bool HasPending() const { return mnDigits > 0; }
    int GetPendingDigits() const { return mnDigits; }
    uint32_t GetPendingValue() const { return mnPending; }
    /// @brief Number of digits that completes the cell (2 for a byte, 4 for a word)
    int GetCellDigits() const { return mbWords ? 4 : 2; }
    /// @brief Add a hexadecimal digit (0-15). Returns true when the cell is complete (the caller writes it).
    bool AddDigit(unsigned digit);
    /// @brief Remove the last digit. Returns false if there was none.
    bool Backspace();
    void ClearPending() { mnDigits = 0; mnPending = 0; }
    /// @brief Text of the cell while typing, right-aligned like the value that will be written ('_' for the missing
    /// digits), for example "__1f". The buffer needs GetCellDigits() + 1 bytes.
    void GetPendingText(char *text) const;

private:
    uint32_t MaxAddress() const { return ADDRESS_SPACE - GetCellSize(); }
    static uint32_t NormalizeZone(uint32_t zoneAddress, int lines);

    uint32_t mnAddress = 0;
    bool mbWords = true;
    uint32_t mnPending = 0;
    int mnDigits = 0;
};

/// @brief Cells written by the user at the current stop (shown in a distinct colour until the next stop), and whether
/// the read-back differed. Byte granularity. When full, the oldest entry is dropped.
class MemoryWriteMarks
{
public:
    enum class Kind : uint8_t
    {
        None,
        Written,    ///< Written by the user, read-back OK (or not read back: I/O)
        Differs,    ///< Written by the user, the read-back differs (ROM, not writable...)
    };

    static const int MAX_ENTRIES = 64;

    void Clear() { mnCount = 0; }
    /// @brief Record a write of size bytes (1 or 2) at address. An older entry of the same bytes is replaced.
    void Add(uint32_t address, uint32_t size, bool readBackDiffers);
    Kind Lookup(uint32_t byteAddress) const;
    int GetCount() const { return mnCount; }

private:
    struct Entry
    {
        uint32_t address;
        uint8_t differs;
    };
    void RemoveAt(int index);
    void AddByte(uint32_t address, bool differs);

    Entry maEntries[MAX_ENTRIES];
    int mnCount = 0;
};
