#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "CommandFsDir.h"
#include "MassStorageManager.h"

#define COMMAND_NAME "FsDir"

#define COMMAND_DESC_TEXT "Directory listing from the embedded filesystem"

void CommandFsDir::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandFsDir::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandFsDir::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
#ifdef USE_MASS_STORAGE
    MassStorageManager::PrintDirectoryListing();
    return true;
#else
    snprintf(returnMessage, messageMaxSize, "Mass storage is not enabled in this build of BricoNeo. Ativate USE_MASS_STORAGE constant.");
    return false;
#endif
}
