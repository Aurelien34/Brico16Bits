#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "CommandReset.h"
#include "PIOManager.h"

#define COMMAND_NAME "Reset"

#define COMMAND_DESC_TEXT "Reset the remote system using watchdog timer"

void CommandReset::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandReset::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandReset::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    PIOManager::ForceWatchdogReset();
    return true;
}
