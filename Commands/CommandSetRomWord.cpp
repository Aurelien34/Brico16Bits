#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandSetRomWord.h"
#include "ROMImageManager.h"

#define COMMAND_NAME "SetRomW"
#define COMMAND_DESC_PARAMETERS "@addr word"
#define COMMAND_DESC_TEXT "Set word \"#word\" at current ROM image address \"@addr\" (address is 0x000000 based, LSB will be ignored)"

void CommandSetRomWord::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandSetRomWord::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandSetRomWord::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandSetRomWord::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    uint32_t address = ParseUint(param1);
    address &= ~1; // Clear the LSB
    uint32_t word = ParseUint(param2);

    if(address > FLASH_IMAGE_SIZE - 2)
    {
        snprintf(returnMessage, messageMaxSize, "The address should be between 0 and 0x%06x", FLASH_IMAGE_SIZE - 2);
        return false;
    }
    else
    {
        snprintf(returnMessage, messageMaxSize, "Set value 0x%04x at address 0x%06x", word, address);
        ROMImageManager::GetImageBaseAddressB()[address] = (uint8_t)word;
        ROMImageManager::GetImageBaseAddressB()[address + 1] = (uint8_t)(word >> 8);
    }
    return true;
}
