#pragma once

#include <pico/stdlib.h>
#include "MemoryMap.h"
#include "StateMachinesDefinitions.h"

#define INPUT_BUFFER_SIZE_POWEROF2   12  // 2<<12 = 4096 bytes / 2048 16 bits words

class InputBuffer
{
public:
    inline static uint16_t * GetBufferAddress() { return (uint16_t *)BASE_ADDRESS_INPUT_RING_BUFFER; }
    static void Clear(bool clearEOT);
    static bool IsEmpty();
    static uint16_t GetValue();
    static bool GetValue(uint16_t *value);
    static bool CheckEOTSignalAndClearEOT();
    static inline uint32_t GetLastBufferCRC() { return lastBufferCRC; }
private:
    static uint16_t *gpNextReadPosition;
    static uint32_t lastBufferCRC;
};