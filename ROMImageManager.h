#pragma once

#include <stdio.h>

#include "MemoryMap.h"

#define FLASH_ROM_BANK_DEFAULT 0
#define FLASH_MAX_ROM_BANKS 4   // each ROM image is 128kB
#define FLASH_IMAGE_SIZE (128 * 1024)


// What has been done on a freshly received ROM image, see ROMImageManager::FinalizeReceivedImage()
struct FinalizeResult
{
    bool byteSwapped;
    bool neoDiag;
};

class ROMImageManager
{
private:
    static uint32_t GetFlashBankAddress(int bankNumber);
public:
    static uint32_t GetBankAddress(int bankNumber);
    static inline uint8_t *GetImageBaseAddressB() { return (uint8_t *)BASE_ADDRESS_IMAGE_IN_RAM; }
    static inline uint16_t *GetImageBaseAddressW() { return (uint16_t *)BASE_ADDRESS_IMAGE_IN_RAM; }
    static inline uint32_t *GetImageBaseAddressDW() { return (uint32_t *)BASE_ADDRESS_IMAGE_IN_RAM; }
    static void SendToFlash(int bankNumber);
    static void GetFromFlash(int bankNumber);
    static void GetFromFlashAsyncStart(int bankNumber);
    static void GetFromFlashAsyncEnd();
    static FinalizeResult FinalizeReceivedImage();
};