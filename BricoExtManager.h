#pragma once

#include <cstdint>
#include "DisplayResult.h"

class BricoExtManager
{
public:
    enum class SpecialModeFlags : int
    {
        None = 0x0000,
        EnableExtensions = 0x0001,
        The68kSendsCommands = 0x0002,
        NeoDiagCRCMirrorComputation = 0x0004,
        RemoteKeyboardMode = 0x0008,
    };

    static bool IsBricoNeoROM();
    static bool IsRemoteKeyboardSupported();
    static int GetExtCount();
    static SpecialModeFlags GetSpecialModeFlags();
    static void *GetExtInfoBaseAddress(int iExtIndex);
    static void GetExtCommandName(int iExtIndex, char *buffer, int bufferSize);
    static void GetExtDescription(int iExtIndex, char *buffer, int bufferSize);
    static DisplayResult::ResultType GetExtResulType(int iExtIndex);
    static uint32_t GetBricoNeoROMVersion();
    static void SetExtParameters(uint32_t uExtAddress, uint32_t uP1, uint32_t uP2, uint32_t uP3);
private:
    static uint32_t GetExtAddress(int iExtIndex);
    static uint8_t GetROMByte(void *startAddress, int iIndex);
    static int GetROMStringLength(char *startAddress);
    static int GetExtCommandNameLength(int iExtIndex);
    static int GetExtDescriptionLength(int iExtIndex);
    static uint32_t GetROMUint32(void *pNotAligned);
    static uint16_t GetROMUint16(void *pNotAligned);
};