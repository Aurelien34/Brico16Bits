#pragma once

#include "DisplayResult.h"

class DisplayResult
{
public:
    enum class ResultType
    {
        Void = 0x0,
        SingleWord = 0x1,
        String = 0x2,
        Dump = 0x3,
    };

    static void Show(ResultType eResultType, char *returnMessage, int messageMaxSize, bool binaryMode);
private:
    static void DisplaySingleWord();
    static void DisplayString();
    static void DisplayDump(bool binaryMode);
};