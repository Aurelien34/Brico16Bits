#include <string.h>
#include <stdio.h>

#include "BricoExtManager.h"
#include "MemoryMap.h"

bool BricoExtManager::IsBricoNeoROM()
{
    // Do we find the magic word ("BricoNeo" byte swapped) at the expected address in ROM?
    return strncmp("rBciNooe", (char *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_MAGIC_WORD), 8) == 0;
}

uint32_t BricoExtManager::GetBricoNeoROMVersion()
{
    uint32_t uVersion = *(uint32_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_VERSION);
    return (uVersion << 16) | (uVersion >> 16);
}

BricoExtManager::SpecialModeFlags BricoExtManager::GetSpecialModeFlags()
{
    uint16_t flags = *(uint16_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_SPECIAL_MODE_FLAGS);
    return (SpecialModeFlags)flags;
}

uint32_t BricoExtManager::GetExtAddress(int iExtIndex)
{
    void*pBaseAdrress = GetExtInfoBaseAddress(iExtIndex);
    return GetROMUint32(pBaseAdrress);
}

DisplayResult::ResultType BricoExtManager::GetExtResulType(int iExtIndex)
{
    void*pBaseAdrress = GetExtInfoBaseAddress(iExtIndex);
    return (DisplayResult::ResultType)GetROMUint16(((uint8_t *)pBaseAdrress) + 4);
}

int BricoExtManager::GetExtCount()
{
    // Extentions are available only for BricoNeo ROM
    if (IsBricoNeoROM() && (int)BricoExtManager::GetSpecialModeFlags() & (int)SpecialModeFlags::EnableExtensions)
    {
        // Iterate through extensions
        int iExtIndex = 0;
        while (GetROMUint32(GetExtInfoBaseAddress(iExtIndex)) != 0xffffffff)
        {
            iExtIndex++;
        }
        return iExtIndex;
    }
    else
    {
        return 0;
    }
}

void * BricoExtManager::GetExtInfoBaseAddress(int iExtIndex)
{
    // Iterate through extensions until we find the expected one
    uint8_t * pExtAddress = (uint8_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_EXTENSION_TABLE);
    int offset = 0;
    for (int iIndex = 0; iIndex < iExtIndex; ++iIndex)
    {
        // Skip the extension address
        offset += 4;

        // Skip the return type
        offset += 2;

        // Skip command name
        int nameLength = GetROMStringLength((char *)pExtAddress + offset) + 1; // Include the terminating zero
        
        // Align on an even size
        if ((nameLength & 1))
        {
            ++nameLength;
        }

        offset += nameLength;

        // Skip description
        int descLength = GetROMStringLength((char *)pExtAddress + offset) + 1; // Include the terminating zero
        
        // Align on an even size
        if ((descLength & 1))
        {
            ++descLength;
        }

        offset += descLength;
    }

    return pExtAddress + offset;
}

int BricoExtManager::GetROMStringLength(char *startAddress)
{
    int iLength = 0;
    while (GetROMByte(startAddress, iLength) != 0)
    {
        iLength++;
    }
    return iLength;
}

uint8_t BricoExtManager::GetROMByte(void *startAddress, int iIndex)
{
    if (iIndex & 1)
        return ((char *)startAddress)[iIndex - 1];
    else
        return ((char *)startAddress)[iIndex + 1];
}

uint32_t BricoExtManager::GetROMUint32(void *pNotAligned)
{
    uint32_t value = 0;

    value = GetROMByte(pNotAligned, 3)
        + (GetROMByte(pNotAligned, 2) << 8)
        + (GetROMByte(pNotAligned, 1) << 16)
        + (GetROMByte(pNotAligned, 0) << 24);

    return value;
}

uint16_t BricoExtManager::GetROMUint16(void *pNotAligned)
{
    uint16_t value = 0;

    value = (GetROMByte(pNotAligned, 1))
        + (GetROMByte(pNotAligned, 0) << 8);

    return value;
}

int BricoExtManager::GetExtCommandNameLength(int iExtIndex)
{
    char *baseAddress = ((char *)GetExtInfoBaseAddress(iExtIndex)) + 4 + 2;
    return GetROMStringLength(baseAddress);
}

int BricoExtManager::GetExtDescriptionLength(int iExtIndex)
{
    char *baseAddress = ((char *)GetExtInfoBaseAddress(iExtIndex)) + 4 + 2;
    int commandNameLength = GetROMStringLength(baseAddress) + 1;
    if (commandNameLength & 1)
    {
        ++commandNameLength;
    }
    return GetROMStringLength(baseAddress + commandNameLength);
}

void BricoExtManager::GetExtCommandName(int iExtIndex, char *buffer, int bufferSize)
{
    char *baseAddress = ((char *)GetExtInfoBaseAddress(iExtIndex)) + 4 + 2;
    int maxLength = bufferSize - 1;
    char *pBuffer = buffer;
    for (int index = 0; index < bufferSize; ++index)
    {
        *pBuffer = GetROMByte(baseAddress, index);
        if (*pBuffer++ == 0)
        {
            break;
        }
    }
    // If the buffer was not large enough...
    buffer[bufferSize - 1] = 0;
}

void BricoExtManager::GetExtDescription(int iExtIndex, char *buffer, int bufferSize)
{
    char *baseAddress = ((char *)GetExtInfoBaseAddress(iExtIndex)) + 4 + 2;
    int commandNameLength = GetROMStringLength(baseAddress) + 1;
    if (commandNameLength & 1)
    {
        ++commandNameLength;
    }
    baseAddress += commandNameLength;

    int maxLength = bufferSize - 1;
    char *pBuffer = buffer;
    for (int index = 0; index < bufferSize; ++index)
    {
        *pBuffer = GetROMByte(baseAddress, index);
        if (*pBuffer++ == 0)
        {
            break;
        }
    }
    // If the buffer was not large enough...
    buffer[bufferSize - 1] = 0;
}

void BricoExtManager::SetExtParameters(uint32_t uExtAddress, uint32_t uP1, uint32_t uP2, uint32_t uP3)
{
    // Write extension address. Comes from the ROM, does not need to be bit swapped
    *(uint32_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_COMMAND_ADDRESS) = uExtAddress;
    // Write P1
    *(uint32_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_COMMAND_PARAMETER_1) = ((uP1 << 16) | (uP1 >> 16));
    // Write P2
    *(uint32_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_COMMAND_PARAMETER_2) = ((uP2 << 16) | (uP2 >> 16));
    // Write P3
    *(uint32_t *)(BASE_ADDRESS_IMAGE_IN_RAM + ROM_OFFSET_COMMAND_PARAMETER_3) = ((uP3 << 16) | (uP3 >> 16));
}
