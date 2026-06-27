#pragma once

#include <cstdint>
#include "DisplayResult.h"

class DisplayResult
{
public:
    class DataProvider
    {
    public:
        virtual uint32_t GetBaseDumpAddress() = 0;
        virtual bool GetValue(uint16_t *value) = 0;
    };

    class DataProviderInputBuffer
    {
    public:
        DataProviderInputBuffer() {}
        virtual uint32_t GetBaseDumpAddress();
        virtual bool GetValue(uint16_t *value);
    };

    class DataProviderMemory
    {
    public:
        DataProviderMemory(uint32_t nBaseDumpAddress, uint16_t nDataLength, uint16_t *pnContent);
        virtual uint32_t GetBaseDumpAddress();
        virtual bool GetValue(uint16_t *value);
    private:
        uint32_t mnBaseDumpAddress;
        uint16_t mnDataRemaining;
        uint16_t *mpnContentPtr;
    };

    enum class ResultType
    {
        Void = 0x0,
        SingleWord = 0x1,
        String = 0x2,
        Dump = 0x3,
    };

    static void Show(ResultType eResultType, char *returnMessage, int messageMaxSize, bool binaryMode);
    static void DisplayDump(bool binaryMode, DataProvider *pDataProvider);
private:
    static void DisplaySingleWord();
    static void DisplayString();
    static void DisplayDump(bool binaryMode);
};