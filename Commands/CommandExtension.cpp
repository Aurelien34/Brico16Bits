#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "BricoExtManager.h"
#include "InputBuffer.h"
#include "DisplayResult.h"
#include "CommandManager.h"

#include "CommandExtension.h"

CommandExtension::CommandExtension(int commandIndex)
{
    mnCommandIndex = commandIndex;
}

void CommandExtension::GetCommandName(char *name) const
{
    BricoExtManager::GetExtCommandName(mnCommandIndex, name, COMMAND_MAX_LENGTH_NAME);
}

void CommandExtension::GetCommandDescriptionText(char *text) const
{
    BricoExtManager::GetExtDescription(mnCommandIndex, text, COMMAND_MAX_LENGTH_DESC_TEXT);
}

bool CommandExtension::Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3)
{
    // Clear the input buffer
    InputBuffer::Clear(true);
    // Get extension address
    void *pExtInfo = BricoExtManager::GetExtInfoBaseAddress(mnCommandIndex);
    // Read extension address in ROM
    uint32_t extAddressInROM;
    memcpy(&extAddressInROM, pExtInfo, 4); // Handle non 4 bytes aligned addresses
    // Read result type in ROM
    DisplayResult::ResultType resultType = BricoExtManager::GetExtResulType(mnCommandIndex);
    // Write extension address to trigger the extension on remote
    // param1 is the extension number!
    BricoExtManager::SetExtParameters(extAddressInROM, ParseUint(param1), ParseUint(param2), ParseUint(param3));
    // Wait for the 68k to be ready
    sleep_ms(1);
    // Start the extension
    BricoExtManager::SetExtParameters(0, 0, 0, 0);
    
    // Read the input buffer
    absolute_time_t timeToWait = from_us_since_boot(time_us_64() + 3000000); // 3 seconds
    bool bFail = false;
    while (!bFail && !InputBuffer::CheckEOTSignalAndClearEOT())
    {
        bFail = time_reached(timeToWait);
    }
    
    if (bFail)
    {
        sprintf(returnMessage, "[Timeout]");
    }
    else
    {
        uint32_t bufferCRC = InputBuffer::GetLastBufferCRC();
        // Compare results CRC to the previous results
        if (bufferCRC != GetLastCommandDataCRC() || !IsLoopMode())
        {
            SetLastCommandDataCRC(bufferCRC);
            if (IsLoopMode())
            {
                CommandManager::HideCursor();
                CommandManager::ClearTerminal();
            }
            DisplayResult::Show(resultType, returnMessage, messageMaxSize);
            if (IsLoopMode())
            {
                CommandManager::ShowCursor();
            }
        }
        else
        {
            // Same results, clear command buffer
            InputBuffer::Clear(true);
        }
    }
    return !bFail;
}

