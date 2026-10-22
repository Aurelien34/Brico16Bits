#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <tusb.h>

#include "ROMUploadManager.h"
#include "ROMImageManager.h"
#include "CommandManager.h"
#include "ActivityLED.h"
#include "PIOManager.h"

bool ROMUploadManager::gbTransferInProgress = false;
bool ROMUploadManager::gbPortWasOpen = false;
bool ROMUploadManager::gbHeaderValid = false;
uint32_t ROMUploadManager::gnReceivedBytes = 0;
uint32_t ROMUploadManager::gnTimeoutMs = ROM_UPLOAD_IDLE_TIMEOUT_MS;
absolute_time_t ROMUploadManager::gtLastDataTime;
uint8_t ROMUploadManager::gaStagingBuffer[ROM_UPLOAD_HEADER_SIZE];

/// @brief Poll the ROM upload serial port. Must be called regularly, after tud_task()
void ROMUploadManager::Loop()
{
    // Host closed the port during a transfer? Then end it as soon as in-flight data is drained
    bool bPortOpen = tud_cdc_n_connected(ROM_UPLOAD_CDC_ITF);
    if (gbTransferInProgress && gbPortWasOpen && !bPortOpen)
    {
        gnTimeoutMs = ROM_UPLOAD_CLOSED_TIMEOUT_MS;
    }
    gbPortWasOpen = bPortOpen;

    if (tud_cdc_n_available(ROM_UPLOAD_CDC_ITF) > 0)
    {
        if (!gbTransferInProgress)
        {
            StartTransfer();
        }

        // Empty the receive FIFO
        uint8_t buffer[64];
        uint32_t count;
        while ((count = tud_cdc_n_read(ROM_UPLOAD_CDC_ITF, buffer, sizeof(buffer))) > 0)
        {
            StoreData(buffer, count);
        }
        gtLastDataTime = get_absolute_time();
    }
    else if (gbTransferInProgress && absolute_time_diff_us(gtLastDataTime, get_absolute_time()) > (int64_t)gnTimeoutMs * 1000)
    {
        EndTransfer();
    }
}

void ROMUploadManager::StartTransfer()
{
    gbTransferInProgress = true;
    gbHeaderValid = false;
    gnReceivedBytes = 0;
    gnTimeoutMs = ROM_UPLOAD_IDLE_TIMEOUT_MS;

    // Drop any status message from a previous transfer the host never read
    tud_cdc_n_write_clear(ROM_UPLOAD_CDC_ITF);

    ActivityLED::SetActivityType(ActivityLED::ActivityType::DataTransfer);
}

void ROMUploadManager::StoreData(const uint8_t *data, uint32_t count)
{
    uint8_t *pImage = ROMImageManager::GetImageBaseAddressB();
    for (uint32_t i = 0; i < count; ++i, ++gnReceivedBytes)
    {
        if (gnReceivedBytes < ROM_UPLOAD_HEADER_SIZE)
        {
            // Keep the header aside until we are sure this is a real ROM transfer
            gaStagingBuffer[gnReceivedBytes] = data[i];
            if (gnReceivedBytes == ROM_UPLOAD_HEADER_SIZE - 1)
            {
                gbHeaderValid = IsHeaderValid();
                if (gbHeaderValid)
                {
                    memcpy(pImage, gaStagingBuffer, ROM_UPLOAD_HEADER_SIZE);
                }
            }
        }
        else if (gbHeaderValid && gnReceivedBytes < FLASH_IMAGE_SIZE)
        {
            pImage[gnReceivedBytes] = data[i];
        }
        // Extra bytes, or bytes of a rejected transfer, are ignored
    }
}

void ROMUploadManager::EndTransfer()
{
    gbTransferInProgress = false;
    ActivityLED::SetActivityType(CommandManager::IsTerminalConnected() ? ActivityLED::ActivityType::TerminalConnected : ActivityLED::ActivityType::Idle);

    if (!gbHeaderValid)
    {
        CommandManager::PrintAsyncMessage("ROM upload ignored: %lu bytes received, not a SP1 ROM image (invalid reset vector)", (unsigned long)gnReceivedBytes);
        SendStatus("ERROR %lu bytes received, ignored: invalid reset vector\r\n", (unsigned long)gnReceivedBytes);
        return;
    }

    FinalizeResult result = ROMImageManager::FinalizeReceivedImage();
    const char *szTruncated = (gnReceivedBytes > FLASH_IMAGE_SIZE) ? ", truncated" : "";
    const char *szByteSwapped = result.byteSwapped ? ", byteswapped" : "";
    const char *szNeoDiag = result.neoDiag ? ", NeoDiag" : "";

    CommandManager::PrintAsyncMessage("ROM received on data port: %lu bytes%s%s%s", (unsigned long)gnReceivedBytes, szTruncated, szByteSwapped, szNeoDiag);
    SendStatus("OK %lu bytes%s%s%s\r\n", (unsigned long)gnReceivedBytes, szTruncated, szByteSwapped, szNeoDiag);

    // Force reset on the remote system
    PIOManager::ForceWatchdogReset();
}

//// @brief Check the staging buffer holds a plausible 68000 header: reset vector upper word must point to the SP1 area (0xc0xxxx), in either endianness
bool ROMUploadManager::IsHeaderValid()
{
    uint16_t resetVectorHigh = ((uint16_t *)gaStagingBuffer)[2];
    return (resetVectorHigh == 0x00c0) || (resetVectorHigh == 0xc000);
}

// @brief Send a status message on the ROM upload port, only if the host has it open (no stale message for the next transfer)
void ROMUploadManager::SendStatus(const char *format, ...)
{
    if (!tud_cdc_n_connected(ROM_UPLOAD_CDC_ITF))
    {
        return;
    }

    char message[96];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Non blocking: message is truncated if the transmit FIFO is full
    tud_cdc_n_write_str(ROM_UPLOAD_CDC_ITF, message);
    tud_cdc_n_write_flush(ROM_UPLOAD_CDC_ITF);
}
