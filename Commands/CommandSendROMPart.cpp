#include <string.h>
#include <stdio.h>
#include <pico/stdio.h>
#include <pico/stdio_usb.h>
#include "CommandManager.h"
#include "CommandSendROMPart.h"
#include "ROMImageManager.h"
#include "ActivityLED.h"
#include "PIOManager.h"

#define COMMAND_NAME "SendROMPart"
#define COMMAND_DESC_PARAMETERS "bytes"
#define COMMAND_DESC_TEXT "Send \"bytes\" bytes from a ROM image from the host to the BricoNeo RAM. Auto byteswap if \"bytes\" is negative"
#define COMMAND_RESULT_DONE "Done"

void CommandSendROMPart::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandSendROMPart::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandSendROMPart::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandSendROMPart::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    bool bByteSwap = false;
    int byteCount = 0;
    if (sscanf(param1, "%d", &byteCount) > 0)
    {
        // Should byteswap?
        if (byteCount < 0)
        {
            byteCount = byteCount * (-1);
            bByteSwap = true;
        }
        byteCount = MIN(byteCount, FLASH_IMAGE_SIZE);
    }
    else
    {
        byteCount = 0;
    }

    // Send green light to the host to send content
    if (CommandManager::IsTerminalConnected())
    {
        printf("> Now send your file\n");
    }
    
    // Read ROM data
    uint8_t *ptr = ROMImageManager::GetImageBaseAddressB();
    bool bFirstRun = true;
    for (int i = 0; i < byteCount; ++i)
    {
        *(ptr++) = CommandManager::GetCharBlocking();
        if (bFirstRun)
        {
            ActivityLED::SetActivityType(ActivityLED::ActivityType::DataTransfer);
            if (*(ptr-1) == 0x0a)
            {
                --ptr;
                i = -1;
            }
            bFirstRun = false;
        }
    }

    // Byteswap if requested to
    if (bByteSwap)
    {
        ptr = ROMImageManager::GetImageBaseAddressB();
        uint8_t *ptr2 = ptr + 1;
        for (int i = 0; i < byteCount; i+=2)
        {
            uint8_t tmp = *ptr;
            *ptr = *ptr2;
            *ptr2 = tmp;
            ptr += 2;
            ptr2 += 2;
        }
    }

    strncpy(returnMessage, COMMAND_RESULT_DONE, messageMaxSize);

    // Force reset on the remote system
    PIOManager::ForceWatchdogReset();

    return true;
}
