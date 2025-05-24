#include <stdio.h>
#include <pico/stdlib.h>
#include <string.h>

#include "CommandManager68k.h"
#include "Commands68k/Command68k.h"
#include "BricoExtManager.h"
#include "InputBuffer.h"

#include "Commands68k/Command68kPrintText.h"
#include "Commands68k/Command68kPrintHex.h"
#include "Commands68k/Command68kSwitchBank.h"

#define BUILTIN_COMMAND_COUNT (sizeof(CommandManager68k::gaBuiltInCommandConstructors)/sizeof(void *))

Command68k * (* CommandManager68k::gaBuiltInCommandConstructors [])() =
{
    // Misc
    []() -> Command68k * { return new Command68kPrintText(); },
    []() -> Command68k * { return new Command68kPrintHex(); },
    []() -> Command68k * { return new Command68kSwitchBank(); },
};

Command68k * CommandManager68k::GetCommand(int commandIndex)
{
    // Generated command
    Command68k *pCmd = NULL;
    
    // Builtin command?
    if (commandIndex < BUILTIN_COMMAND_COUNT)
    {
        pCmd = gaBuiltInCommandConstructors[commandIndex]();
    }
    // Found a command?
    if (pCmd != NULL)
    {
        // Initialize the command
    }
    return pCmd;
}

void CommandManager68k::HandleCommands()
{
    // Check if 68k commands flag is active
    if (BricoExtManager::IsBricoNeoROM()
        && ((int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::The68kSendsCommands))
    {
        // Found something in the input buffer?
        if (InputBuffer::CheckEOTSignalAndClearEOT())
        {
            // Read command name on 4 letters
            char commandName[COMMAND68K_NAME_LENGTH + 1];
            for (int i = 0; i < COMMAND68K_NAME_LENGTH; ++i)
            {
                uint16_t val;
                if (!InputBuffer::GetValue(&val))
                {
                    // The command is not complete
                    return;
                }
                commandName[i] =(uint8_t)val;
            }
            commandName[COMMAND68K_NAME_LENGTH] = 0;
            
            // Get the corresponding command object
            Command68k *cmd = GetCommandByName(commandName);
            if (cmd == NULL)
            {
                // The command was not found
                return;
            }

            // Execute the command
            cmd->Execute();

            // Free command
            delete cmd;

            // Ready buffer until its end
            InputBuffer::Clear(false);
        }
    }
}

Command68k * CommandManager68k::GetCommandByName(char * name)
{
    Command68k *cmd = NULL;

    char commandName[5];
    for (int commandIndex = 0; commandIndex < BUILTIN_COMMAND_COUNT; ++commandIndex)
    {
        cmd = GetCommand(commandIndex);
        cmd->GetName(commandName);
        if (strncmp(name, commandName, COMMAND68K_NAME_LENGTH) == 0)
        {
            // The command found is the expected one
            break;
        }
        else
        {
            // Not the expected command, delete it!
            delete cmd;
            cmd = NULL;
        }
    }

    return cmd;    
}
