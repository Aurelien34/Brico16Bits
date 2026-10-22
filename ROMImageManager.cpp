#include <stdio.h>
#include <malloc.h>
#include <hardware/dma.h>
#include <hardware/structs/bus_ctrl.h>
#include <pico/stdlib.h>
#include <hardware/flash.h>
#include <pico/multicore.h>

#include "MemoryMap.h"
#include "ROMImageManager.h"
#include "DMAManager.h"
#include "NeoDiagImageBuilder.h"

#define FLASH_ROM_START 0x10000000
#define FLASH_ROM_SIZE (2 * 1024 * 1024)    // 2 megabytes


/// @brief Computes the base address of the requested flash memory bank.
/// @param bankNumber Zero based bank number
/// @return Offset from the start of the flash memory
uint32_t ROMImageManager::GetBankAddress(int bankNumber)
{
    return FLASH_ROM_START + GetFlashBankAddress(bankNumber);
}

/// @brief Computes the base address (in flash reference, starting at offset 0x00000000) of the requested flash memory bank.
/// @param bankNumber Zero based bank number
/// @return Offset from the start of the flash memory
uint32_t ROMImageManager::GetFlashBankAddress(int bankNumber)
{
    return FLASH_ROM_SIZE - (bankNumber + 1) * FLASH_IMAGE_SIZE;
}

/// @brief Send the current image in RAM to the Flash memory
/// @param bankNumber Memory bank to send the image to (maximum depends on Flash memory usage for the RP2040 code). See FLASH_MAX_ROM_BANKS
void ROMImageManager::SendToFlash(int bankNumber)
{
    // Warning: Core1 must have called multicore_lockout_victim_init() function first
    //multicore_lockout_start_blocking();
    uint32_t status = save_and_disable_interrupts();
    flash_range_erase(GetFlashBankAddress(bankNumber), FLASH_IMAGE_SIZE);
    flash_range_program(GetFlashBankAddress(bankNumber), GetImageBaseAddressB(), FLASH_IMAGE_SIZE);
    restore_interrupts(status);
    //multicore_lockout_end_blocking();
}

/// @brief Send the requested ROM image from flash memory to RAM
/// @param bankNumber Memory bank to send the image to (maximum depends on Flash memory usage for the RP2040 code). See FLASH_MAX_ROM_BANKS
void ROMImageManager::GetFromFlash(int bankNumber)
{
    ROMImageManager::GetFromFlashAsyncStart(bankNumber);
    ROMImageManager::GetFromFlashAsyncEnd();
}

/// @brief Send the requested ROM image from flash memory to RAM using DMA. Should call GetFromFlashAsyncEnd() to ensure the copy is complete
/// @param bankNumber Memory bank to send the image to (maximum depends on Flash memory usage for the RP2040 code). See FLASH_MAX_ROM_BANKS
void ROMImageManager::GetFromFlashAsyncStart(int bankNumber)
{
    DMAManager::DMAStartCopyDW((void *)(FLASH_ROM_START + GetFlashBankAddress(bankNumber)), (void *)BASE_ADDRESS_IMAGE_IN_RAM, FLASH_IMAGE_SIZE);
}

/// @brief Wait for previous call to GetFromFlashAsyncStart() to complete
void ROMImageManager::GetFromFlashAsyncEnd()
{
    DMAManager::DMAEndCopyDW();
}

/// @brief Post-process a ROM image freshly received in RAM (mass storage or ROM upload serial port): byteswap if needed, then NeoDiag processing
/// @return true if the image has been byteswapped
bool ROMImageManager::FinalizeReceivedImage()
{
    // Detect endianness using the reset vector upper word
    uint16_t *pImage = GetImageBaseAddressW();
    bool romNeedsByteSwap = (pImage[2] == 0xc000);

    // Byteswap if needed
    if (romNeedsByteSwap)
    {
#ifdef MASS_STORAGE_DEBUG
        printf("The file needs byteswap\n");
#endif

        // Byteswap the whole image
        for (int i = 0; i < FLASH_IMAGE_SIZE / 2; ++i)
        {
            uint16_t val = pImage[i];
            pImage[i] = (val >> 8) + (val << 8);
        }
    }
    else
    {
#ifdef MASS_STORAGE_DEBUG
        printf("The file does not need byteswap\n");
#endif
    }

    // Handle NeoDiag ROM image with BricoNeo flags
    if (NeoDiagImageBuilder::CheckAndBuildImage())
    {
        printf("NeoDiag image detected and processed (CRC32 computation)\n");
    }
    else
    {
#ifdef MASS_STORAGE_DEBUG
        printf("No NeoDiag image detected\n");
#endif
    }

    return romNeedsByteSwap;
}
