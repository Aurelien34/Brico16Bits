#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandReadWord.h"
#include "InputBuffer.h"

#define COMMAND_NAME "ReadWord"
#define COMMAND_DESC_TEXT "Read one word"

void CommandReadWord::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandReadWord::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandReadWord::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    uint16_t value;
    if (InputBuffer::GetValue(&value))
    {
        sprintf(returnMessage, "0x%04x", value);
    }
    else
    {
        // Nothing to return
        returnMessage[0] = 0;
    }
    return true;
}
