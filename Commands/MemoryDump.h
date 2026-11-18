#pragma once

#include <cstdint>

/// Maximum size of a dump zone in bytes (32 lines of 16 bytes, 256 words = READ_MEM maximum)
#define MEMORY_DUMP_MAX_BYTES 512

/// @brief A zone of memory: start address and bytes
struct MemoryDumpZone
{
    uint32_t address;
    uint16_t size;      // Number of valid bytes, 0 = empty
    uint8_t bytes[MEMORY_DUMP_MAX_BYTES];
};

/// @brief Keeps the current and previous memory dumps to highlight modified bytes
class MemoryDump
{
public:
    MemoryDump();

    /// @brief Forget both dumps
    void Clear();

    /// @brief New dump becomes current, old current becomes previous (shift).
    ///        Size is clamped to MEMORY_DUMP_MAX_BYTES.
    void Push(uint32_t address, const uint8_t *bytes, uint16_t size);

    /// @brief Same as Push, from 16-bit words as returned by DebugLink::ReadMemory
    ///        (each word is stored big-endian, like on the 68000 bus). Size is wordCount*2 bytes.
    void PushWords(uint32_t address, const uint16_t *words, uint16_t wordCount);

    /// @brief Replace the current dump without shifting (previous is kept)
    void SetCurrent(uint32_t address, const uint8_t *bytes, uint16_t size);

    /// @brief Discard the previous dump (e.g. when the displayed address changes)
    void ClearPrevious();

    bool HasCurrent() const { return mCurrent.size != 0; }
    bool HasPrevious() const { return mPrevious.size != 0; }
    const MemoryDumpZone& Current() const { return mCurrent; }
    const MemoryDumpZone& Previous() const { return mPrevious; }

    /// @brief true only if address belongs to both dumps and the byte differs
    bool IsByteChanged(uint32_t address) const;

    /// @brief Read a byte of the current dump. Returns false if address is outside.
    bool GetByte(uint32_t address, uint8_t *value) const;

private:
    static void Fill(MemoryDumpZone &zone, uint32_t address, const uint8_t *bytes, uint16_t size);
    static bool Contains(const MemoryDumpZone &zone, uint32_t address);

    MemoryDumpZone mCurrent;
    MemoryDumpZone mPrevious;
};
