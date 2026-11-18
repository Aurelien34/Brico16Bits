#include "MemoryCursor.h"

void MemoryCursor::Reset(uint32_t zoneAddress, bool words)
{
    mbWords = words;
    mnAddress = (zoneAddress & (ADDRESS_SPACE - 1)) & ~(GetCellSize() - 1);
    ClearPending();
}

void MemoryCursor::SetWordMode(bool words)
{
    mbWords = words;
    mnAddress &= ~(GetCellSize() - 1);
    ClearPending();
}

/// @brief Zone address aligned on a line, zone inside the address space (same rule as the memory reads)
uint32_t MemoryCursor::NormalizeZone(uint32_t zoneAddress, int lines)
{
    uint32_t bytes = lines > 0 ? (uint32_t)lines * BYTES_PER_LINE : 0;
    uint32_t address = zoneAddress & (ADDRESS_SPACE - 1) & ~(BYTES_PER_LINE - 1);
    if (bytes > 0 && address > ADDRESS_SPACE - bytes)
    {
        address = ADDRESS_SPACE - bytes;
    }
    return address;
}

bool MemoryCursor::EnsureVisible(uint32_t *zoneAddress, int lines)
{
    uint32_t original = *zoneAddress;
    if (lines <= 0)
    {
        return false;
    }
    uint32_t zone = NormalizeZone(original, lines);
    uint32_t bytes = (uint32_t)lines * BYTES_PER_LINE;
    uint32_t lineOfCursor = mnAddress & ~(BYTES_PER_LINE - 1);
    if (lineOfCursor < zone)
    {
        zone = lineOfCursor;
    }
    else if (lineOfCursor >= zone + bytes)
    {
        zone = lineOfCursor - bytes + BYTES_PER_LINE;
    }
    *zoneAddress = zone;
    return zone != original;
}

bool MemoryCursor::MoveCells(int count, uint32_t *zoneAddress, int lines)
{
    ClearPending();
    int64_t target = (int64_t)mnAddress + (int64_t)count * GetCellSize();
    if (target < 0)
    {
        target = 0;
    }
    if (target > (int64_t)MaxAddress())
    {
        target = MaxAddress();
    }
    mnAddress = (uint32_t)target & ~(GetCellSize() - 1);
    return EnsureVisible(zoneAddress, lines);
}

bool MemoryCursor::MoveLines(int count, uint32_t *zoneAddress, int lines)
{
    ClearPending();
    int64_t target = (int64_t)mnAddress + (int64_t)count * (int64_t)BYTES_PER_LINE;
    if (target >= 0 && target <= (int64_t)MaxAddress())
    {
        mnAddress = (uint32_t)target;
    }
    return EnsureVisible(zoneAddress, lines);
}

bool MemoryCursor::MovePage(int direction, uint32_t *zoneAddress, int lines)
{
    ClearPending();
    if (lines <= 0)
    {
        return false;
    }
    uint32_t original = *zoneAddress;
    uint32_t zone = NormalizeZone(original, lines);
    uint32_t bytes = (uint32_t)lines * BYTES_PER_LINE;
    uint32_t column = mnAddress & (BYTES_PER_LINE - 1);
    if (direction < 0)
    {
        uint32_t newZone = zone >= bytes ? zone - bytes : 0;
        uint32_t shift = zone - newZone;
        if (shift == 0)
        {
            mnAddress = column;     // Already at the top: first line
        }
        else
        {
            mnAddress -= shift;
        }
        zone = newZone;
    }
    else
    {
        uint32_t maxZone = ADDRESS_SPACE - bytes;
        uint32_t newZone = zone + bytes <= maxZone ? zone + bytes : maxZone;
        uint32_t shift = newZone - zone;
        if (shift == 0)
        {
            mnAddress = zone + bytes - BYTES_PER_LINE + column;     // Already at the bottom: last line
        }
        else
        {
            mnAddress += shift;
        }
        zone = newZone;
    }
    if (mnAddress > MaxAddress())
    {
        mnAddress = MaxAddress();
    }
    mnAddress &= ~(GetCellSize() - 1);
    *zoneAddress = zone;
    EnsureVisible(zoneAddress, lines);
    return *zoneAddress != original;
}

bool MemoryCursor::AddDigit(unsigned digit)
{
    if (digit > 15)
    {
        return false;
    }
    if (mnDigits < GetCellDigits())
    {
        mnPending = (mnPending << 4) | digit;
        ++mnDigits;
    }
    return mnDigits >= GetCellDigits();
}

bool MemoryCursor::Backspace()
{
    if (mnDigits == 0)
    {
        return false;
    }
    mnPending >>= 4;
    --mnDigits;
    return true;
}

void MemoryCursor::GetPendingText(char *text) const
{
    int total = GetCellDigits();
    for (int i = 0; i < total; ++i)
    {
        int fromRight = total - 1 - i;      // 0 = least significant digit
        if (fromRight < mnDigits)
        {
            unsigned d = (mnPending >> (fromRight * 4)) & 0xf;
            text[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        }
        else
        {
            text[i] = '_';
        }
    }
    text[total] = 0;
}

void MemoryWriteMarks::RemoveAt(int index)
{
    for (int i = index; i + 1 < mnCount; ++i)
    {
        maEntries[i] = maEntries[i + 1];
    }
    --mnCount;
}

void MemoryWriteMarks::AddByte(uint32_t address, bool differs)
{
    for (int i = 0; i < mnCount; ++i)
    {
        if (maEntries[i].address == address)
        {
            RemoveAt(i);
            break;
        }
    }
    if (mnCount == MAX_ENTRIES)
    {
        RemoveAt(0);
    }
    maEntries[mnCount].address = address;
    maEntries[mnCount].differs = differs ? 1 : 0;
    ++mnCount;
}

void MemoryWriteMarks::Add(uint32_t address, uint32_t size, bool readBackDiffers)
{
    for (uint32_t i = 0; i < size; ++i)
    {
        AddByte(address + i, readBackDiffers);
    }
}

MemoryWriteMarks::Kind MemoryWriteMarks::Lookup(uint32_t byteAddress) const
{
    for (int i = 0; i < mnCount; ++i)
    {
        if (maEntries[i].address == byteAddress)
        {
            return maEntries[i].differs ? Kind::Differs : Kind::Written;
        }
    }
    return Kind::None;
}
