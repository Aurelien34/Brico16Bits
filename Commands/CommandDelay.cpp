#include <string.h>
#include <stdio.h>
#include <pico/stdlib.h>
#include "CommandDelay.h"

#define COMMAND_NAME "Delay"
#define COMMAND_DESC_PARAMETERS "x"
#define COMMAND_DESC_TEXT "Delay #x milliseconds"

void CommandDelay::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandDelay::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandDelay::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandDelay::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    uint16_t value = ParseUint(param1);
    sleep_ms(value);
    return true;
}
