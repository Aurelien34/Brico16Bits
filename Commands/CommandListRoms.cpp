#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cstdint>

#include "ROMImageManager.h"
#include "DisplayResult.h"

#include "CommandListRoms.h"

#define DUMP_LENGTH (128+8)
#define COMMAND_NAME "ListRoms"

#define COMMAND_DESC_TEXT "List ROM banks content (shows a dump of the first bytes of each bank)"

void CommandListRoms::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandListRoms::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandListRoms::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    printf("Current ROM:\n");
    DisplayResult::DataProviderMemory dataMemory(0, DUMP_LENGTH, (uint16_t*)BASE_ADDRESS_IMAGE_IN_RAM);
    DisplayResult::DisplayDump(false, (DisplayResult::DataProvider *)&dataMemory);
    
    for (int iBankNumber = 0; iBankNumber < FLASH_MAX_ROM_BANKS; ++iBankNumber)
    {
        printf("Bank #%d:\n", iBankNumber);
        DisplayResult::DataProviderMemory dataMemoryBank(0, DUMP_LENGTH, (uint16_t*)ROMImageManager::GetBankAddress(iBankNumber));
        DisplayResult::DisplayDump(false, (DisplayResult::DataProvider *)&dataMemoryBank);
    }

    return true;
}
