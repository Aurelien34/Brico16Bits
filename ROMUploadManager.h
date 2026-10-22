#pragma once

#include <pico/stdlib.h>

// Second USB serial port, dedicated to ROM images upload (first one is the console)
#define ROM_UPLOAD_CDC_ITF 1

// End of transfer is detected after this delay without receiving any data...
#define ROM_UPLOAD_IDLE_TIMEOUT_MS 300
// ... or after this (shorter) delay once the host has closed the port (DTR dropped), to drain in-flight data
#define ROM_UPLOAD_CLOSED_TIMEOUT_MS 20

// The 68000 header (initial SSP + reset vector) is kept in a staging buffer until it can be validated
// Transfers without a plausible SP1 reset vector are ignored (protects the ROM image against port probing, e.g. Linux ModemManager)
#define ROM_UPLOAD_HEADER_SIZE 8

class ROMUploadManager
{
public:
    static void Loop();
private:
    static void StartTransfer();
    static void StoreData(const uint8_t *data, uint32_t count);
    static void EndTransfer();
    static bool IsHeaderValid();
    static void SendStatus(const char *format, ...);

    static bool gbTransferInProgress;
    static bool gbPortWasOpen;
    static bool gbHeaderValid;
    static uint32_t gnReceivedBytes;
    static uint32_t gnTimeoutMs;
    static absolute_time_t gtLastDataTime;
    static uint8_t gaStagingBuffer[ROM_UPLOAD_HEADER_SIZE];
};
