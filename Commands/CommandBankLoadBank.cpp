#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandBankLoadBank.h"
#include "ROMImageManager.h"
#include "PIOManager.h"

#define COMMAND_NAME "LoadBank"
#define COMMAND_DESC_PARAMETERS "x"
#define COMMAND_DESC_TEXT "Load memory bank #x in RAM"

void CommandBankLoadBank::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandBankLoadBank::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandBankLoadBank::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandBankLoadBank::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    int bankNumber = atoi(param1);
    if (bankNumber >= 0 && bankNumber < FLASH_MAX_ROM_BANKS)
    {
        ROMImageManager::GetFromFlash(bankNumber);

        // Force reset on the remote system
        PIOManager::ForceWatchdogReset();
        
        return true;
    }
    else
    {
        snprintf(returnMessage, messageMaxSize, "Authorized bank numbers are between 0 and %d", FLASH_MAX_ROM_BANKS - 1);
        return false;
    }
}
