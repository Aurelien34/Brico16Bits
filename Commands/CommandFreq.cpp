#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "PIOManager.h"
#include "PinDefinitions.h"

#include "CommandFreq.h"

#define COMMAND_NAME "Freq"
#define COMMAND_DESC_TEXT "Frequency read on the input pin"

void CommandFreq::GetCommandName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND_MAX_LENGTH_NAME);
}

void CommandFreq::GetCommandDescriptionText(char *text) const
{
    strncpy(text, COMMAND_DESC_TEXT, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandFreq::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    mnFrequency = PIOManager::ComputeFrequency();
    if (mnFrequency < 100)
    {
        int gpioState = (gpio_get(PIN_EXTENDED_IN) ? 1 : 0);
        snprintf(returnMessage, messageMaxSize, "Frequency < 100Hz. Pin state: %s", gpioState ? "High" : "Low");
        // Compute fake CRC in order to have a switching state when the frequency is stuck to 100-Hz
        if (gpioState)
            mnFrequency += 1 << 31;
        else
            mnFrequency += 1 << 30;
    }
    else if (mnFrequency < 1000)
        snprintf(returnMessage, messageMaxSize, "Frequency: %d Hz", mnFrequency);
    else if (mnFrequency < 1000000)
        snprintf(returnMessage, messageMaxSize, "Frequency: %d.%03d kHz", mnFrequency / 1000, mnFrequency % 1000);
    else
        snprintf(returnMessage, messageMaxSize, "Frequency: %d.%06d MHz", mnFrequency / 1000000, mnFrequency % 1000000);

    return true;
}
