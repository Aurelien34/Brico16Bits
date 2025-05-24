#include "Command68kSwitchBank.h"
#include "ROMImageManager.h"
#include "PIOManager.h"

#define COMMAND_NAME "SWBNK"

void Command68kSwitchBank::GetName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND68K_NAME_LENGTH);
}

void Command68kSwitchBank::Execute()
{
    // The word should be the bank number
    uint16_t bankNumber;
    if (InputBuffer::GetValue(&bankNumber))
    {
        if (bankNumber >= 0 && bankNumber < FLASH_MAX_ROM_BANKS)
        {
            ROMImageManager::GetFromFlash(bankNumber);

            // Force reset on the remote system
            PIOManager::ForceWatchdogReset();
        }
    }
}
