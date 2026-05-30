#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <hardware/flash.h>
#include <pico/multicore.h>
#include <pico/bootrom.h>

#include "CommandBootSel.h"

#define COMMAND_NAME "BootSel"

#define COMMAND_DESC_TEXT "Switch the BricoNeo to bootset mode in order to upload a new firmware"

void CommandBootSel::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandBootSel::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandBootSel::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    //multicore_reset_core1();
    //multicore_launch_core1(NULL);
    reset_usb_boot(0,0);

    return true;
}
