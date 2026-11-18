#include "MemoryDump.h"
#include <cstring>

MemoryDump::MemoryDump()
{
    Clear();
}

void MemoryDump::Clear()
{
    mCurrent.address = 0;
    mCurrent.size = 0;
    mPrevious.address = 0;
    mPrevious.size = 0;
}

void MemoryDump::ClearPrevious()
{
    mPrevious.size = 0;
}

void MemoryDump::Fill(MemoryDumpZone &zone, uint32_t address, const uint8_t *bytes, uint16_t size)
{
    if (size > MEMORY_DUMP_MAX_BYTES)
    {
        size = MEMORY_DUMP_MAX_BYTES;
    }
    zone.address = address;
    zone.size = size;
    if (size != 0)
    {
        memcpy(zone.bytes, bytes, size);
    }
}

bool MemoryDump::Contains(const MemoryDumpZone &zone, uint32_t address)
{
    return zone.size != 0 && address >= zone.address && (address - zone.address) < zone.size;
}

void MemoryDump::Push(uint32_t address, const uint8_t *bytes, uint16_t size)
{
    mPrevious = mCurrent;
    Fill(mCurrent, address, bytes, size);
}

void MemoryDump::SetCurrent(uint32_t address, const uint8_t *bytes, uint16_t size)
{
    Fill(mCurrent, address, bytes, size);
}

void MemoryDump::PushWords(uint32_t address, const uint16_t *words, uint16_t wordCount)
{
    uint8_t buffer[MEMORY_DUMP_MAX_BYTES];
    if (wordCount > MEMORY_DUMP_MAX_BYTES / 2)
    {
        wordCount = MEMORY_DUMP_MAX_BYTES / 2;
    }
    for (uint16_t i = 0; i < wordCount; i++)
    {
        buffer[2 * i] = (uint8_t)(words[i] >> 8);
        buffer[2 * i + 1] = (uint8_t)(words[i] & 0xFF);
    }
    Push(address, buffer, (uint16_t)(wordCount * 2));
}

bool MemoryDump::IsByteChanged(uint32_t address) const
{
    if (!Contains(mCurrent, address) || !Contains(mPrevious, address))
    {
        return false;
    }
    return mCurrent.bytes[address - mCurrent.address] != mPrevious.bytes[address - mPrevious.address];
}

bool MemoryDump::GetByte(uint32_t address, uint8_t *value) const
{
    if (!Contains(mCurrent, address))
    {
        return false;
    }
    *value = mCurrent.bytes[address - mCurrent.address];
    return true;
}
