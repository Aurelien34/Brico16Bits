#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandReadWords.h"
#include "InputBuffer.h"

#define COMMAND_NAME "ReadWords"
#define COMMAND_DESC_TEXT "Read the whole buffer"

void CommandReadWords::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandReadWords::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandReadWords::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    // Data will be returned directly through stdout
    returnMessage[0] = 0;

    // Get as many words as available
    uint16_t value;
    while (InputBuffer::GetValue(&value))
    {
        printf("0x%04x\n", value);
    }

    return true;
}
