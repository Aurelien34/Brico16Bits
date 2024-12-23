#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CommandTrace.h"
#include "PIOManager.h"
#include "DMAManager.h"
#include "InputBuffer.h"

#define COMMAND_NAME "Trace"
#define COMMAND_DESC_PARAMETERS "on/off"
#define COMMAND_DESC_TEXT "Activate or reactivate requested addresses tracing to the Output buffer. Will stop after %d accesses"

void CommandTrace::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandTrace::GetCommandDescriptionParameters(char *parameters) const
{
    strncpy(parameters, COMMAND_DESC_PARAMETERS, COMMAND_MAX_LENGTH_DESC_PARAMETERS);
}

void CommandTrace::GetCommandDescriptionText(char *text) const
{
    snprintf(text, COMMAND_MAX_LENGTH_DESC_TEXT, COMMAND_DESC_TEXT, (1 << INPUT_BUFFER_SIZE_POWEROF2 - 1) - 1);
}

bool CommandTrace::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    bool bActivate;
    if (strcmp(param1, "on") == 0)
    {
        bActivate = true;
        strncpy(returnMessage, "Trace is on", messageMaxSize);
    }
    else
    {
        if (strcmp(param1, "off") == 0)
        {
            bActivate = false;
            strncpy(returnMessage, "Trace is off", messageMaxSize);
        }
        else
        {
            snprintf(returnMessage, messageMaxSize, "Choose on (activate) or off (disactivate)");
            return false;
        }
    }

    // Change PIO program
    PIOManager::InitBusWatcherStateMachine(bActivate ? PIOManager::BusWatcherType::Addresses : PIOManager::BusWatcherType::Port);

    // Change DMA configuration
    DMAManager::InitChannelBusWatcher(InputBuffer::GetBufferAddress(), true);

    // Clear the buffer
    if (bActivate)
    {
        InputBuffer::Clear(true);
    }

    return true;
}
