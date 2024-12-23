#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandBankStoreBank.h"
#include "ROMImageManager.h"
#include "ActivityLED.h"

#define COMMAND_NAME "StoreBank"
#define COMMAND_DESC_PARAMETERS "x"
#define COMMAND_DESC_TEXT " Store RAM in bank #x"

void CommandBankStoreBank::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandBankStoreBank::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandBankStoreBank::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandBankStoreBank::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    int bankNumber = atoi(param1);
    if (bankNumber >= 0 && bankNumber < FLASH_MAX_ROM_BANKS)
    {
        ActivityLED::SetActivityType(ActivityLED::ActivityType::Off);
        ActivityLED::ForceState(false);
        ROMImageManager::SendToFlash(bankNumber);
        return true;
    }
    else
    {
        snprintf(returnMessage, messageMaxSize, "Authorized bank numbers are between 0 and %d", FLASH_MAX_ROM_BANKS - 1);
        return false;
    }
}
