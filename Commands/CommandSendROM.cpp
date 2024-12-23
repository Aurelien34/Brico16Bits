#include <string.h>
#include <stdio.h>
#include <pico/stdio.h>
#include <pico/stdio_usb.h>
#include "CommandManager.h"
#include "CommandSendROM.h"
#include "ROMImageManager.h"
#include "ActivityLED.h"
#include "PIOManager.h"

#define COMMAND_NAME "SendROM"
#define COMMAND_DESC_TEXT "Send a ROM image from the host to the BricoNeo RAM. This operation is easier using mass storage mode"
#define COMMAND_RESULT_DONE "Done"

void CommandSendROM::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandSendROM::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandSendROM::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    // Send green light to the host to send content
    if (CommandManager::IsTerminalConnected())
    {
        printf("> Now send your file\n");
    }
    
    // Read ROM data
    uint8_t *ptr = ROMImageManager::GetImageBaseAddressB();
    bool bFirstRun = true;
    for (int i = 0; i < FLASH_IMAGE_SIZE; ++i)
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

    strncpy(returnMessage, COMMAND_RESULT_DONE, messageMaxSize);

    // Force reset on the remote system
    PIOManager::ForceWatchdogReset();

    return true;
}
