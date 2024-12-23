#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "CommandComment.h"

#define COMMAND_NAME COMMAND_NAME_COMMENT

#define COMMAND_DESC_TEXT "Comment: ignore what follows \"" COMMAND_NAME "\". Useful when pasting scripts in the terminal"

void CommandComment::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandComment::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandComment::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    return true;
}
