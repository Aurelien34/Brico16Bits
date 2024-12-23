#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandClearBuffer.h"
#include "InputBuffer.h"

#define COMMAND_NAME "ClearBuffer"
#define COMMAND_DESC_TEXT "Clear the buffer"

void CommandClearBuffer::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandClearBuffer::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandClearBuffer::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    // Clear the buffer
    InputBuffer::Clear(true);
    strncpy(returnMessage, "Buffer cleared", messageMaxSize);
    return true;
}
