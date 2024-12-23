#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include "CommandFree.h"

#define COMMAND_NAME "Free"
#define COMMAND_DESC_TEXT "Available RAM on BricoNeo board"

extern char __StackLimit, __bss_end__;

void CommandFree::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandFree::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandFree::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    struct mallinfo m = mallinfo();
    uint32_t freeRAM = &__StackLimit - &__bss_end__ - m.uordblks;
    snprintf(returnMessage, messageMaxSize, "Free RAM %d bytes", freeRAM);

    return true;
}
