#include <stdio.h>
#include <stdarg.h>
#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <tusb.h>
#include <pico/unique_id.h>

#include "CommandManager.h"
#include "PinDefinitions.h"
#include "ROMImageManager.h"
#include "MassStorageManager.h"
#include "ROMUploadManager.h"
#include "BricoExtManager.h"

#include "Commands/Command.h"
#include "Commands/CommandBankLoadBank.h"
#include "Commands/CommandBankStoreBank.h"
#include "Commands/CommandSetRomWord.h"
#include "Commands/CommandSendROMPart.h"
#include "Commands/CommandReadWord.h"
#include "Commands/CommandReadWords.h"
#include "Commands/CommandClearBuffer.h"
#include "Commands/CommandTrace.h"
#include "Commands/CommandFree.h"
#include "Commands/CommandFreq.h"
#include "Commands/CommandLs.h"
#include "Commands/CommandReset.h"
#include "Commands/CommandBootSel.h"
#include "Commands/CommandDebug.h"
#include "Commands/CommandComment.h"
#include "Commands/CommandDelay.h"
#include "Commands/CommandListRoms.h"

#include "Commands/CommandExtension.h"

#include "ActivityLED.h"

#define BUILTIN_COMMAND_COUNT (sizeof(gaBuiltInCommandConstructors)/sizeof(void *))

#define MAX_COMMAND_COUNT 10
#define MAX_COMMAND_WIDTH 100
#define MAX_LENGTH_RETURN_MESSAGE (1024 - 1)
#define MAX_LENGTH_ASYNC_MESSAGE 127
#define MAX_LENGTH_PENDING_MESSAGES (512 - 1)

// Colors can be found here: https://tintin.mudhalla.net/info/256color/
#define COLOR_INDEX_NORMAL                  7
#define COLOR_INDEX_COMMAND_NAME            12
#define COLOR_INDEX_COMMAND_PARAMETERS      5
#define COLOR_INDEX_COMMAND_DESCRIPTION     7
#define COLOR_INDEX_PROMPT                  13
#define COLOR_INDEX_ERROR                   9
#define COLOR_INDEX_RETURN_MESSAGES         10
#define COLOR_INDEX_SECTION_NAME            14
#define COLOR_INDEX_COMMENT                 2
#define COLOR_INDEX_KEYBOARD_MODE_COMMAND   46
#define COLOR_INDEX_KEYBOARD_MODE_REMOTE    201

#define REMOTE_KEYBOARD_ACTIVATION_KEY  0x09 // TAB

Command * (* CommandManager::gaBuiltInCommandConstructors [])() =
{
    // Misc
    []() -> Command * { return new CommandBootSel(); },
    []() -> Command * { return new CommandComment(); },
    []() -> Command * { return new CommandDelay(); },
    []() -> Command * { return new CommandDebug(); },
    // ROM management
    []() -> Command * { return new CommandSendROMPart(); },
    []() -> Command * { return new CommandBankLoadBank(); },
    []() -> Command * { return new CommandBankStoreBank(); },
    []() -> Command * { return new CommandSetRomWord(); },
    []() -> Command * { return new CommandListRoms(); },
    // Output
    []() -> Command * { return new CommandReadWord(); },
    []() -> Command * { return new CommandReadWords(); },
    []() -> Command * { return new CommandClearBuffer(); },
    // System
    []() -> Command * { return new CommandTrace(); },
    []() -> Command * { return new CommandFree(); },
    []() -> Command * { return new CommandFreq(); },
    []() -> Command * { return new CommandLs(); },
    []() -> Command * { return new CommandReset(); },
};

bool CommandManager::gbPromptDisplayedOnce = false;
bool CommandManager::gbKeyboardModeActive = false;
bool CommandManager::gbKeyboardModeSerialReady = false;
bool CommandManager::gbCommandRunning = false;
char CommandManager::gcPendingMessages[MAX_LENGTH_PENDING_MESSAGES + 1] {0};
char CommandManager::gcCurrentCommand[MAX_COMMAND_WIDTH + 1] {0};
char CommandManager::gcPreviousCommand[MAX_COMMAND_WIDTH + 1] {0};

Command * CommandManager::GetCommand(int commandIndex)
{
    // Generated command
    Command *pCmd = NULL;
    
    // Builtin command?
    if (commandIndex < BUILTIN_COMMAND_COUNT)
    {
        pCmd = gaBuiltInCommandConstructors[commandIndex]();
    }
    // Extension command?
    else if (commandIndex - BUILTIN_COMMAND_COUNT < BricoExtManager::GetExtCount())
    {
        pCmd = new CommandExtension(commandIndex - BUILTIN_COMMAND_COUNT);
    }
    
    // Found a command?
    if (pCmd != NULL)
    {
        // Initialize the command
        pCmd->SetLastCommandDataCRC(0xffffffff);
        pCmd->SetLoopMode(false);
        pCmd->SetOutputBinaryMode(false);
    }
    return pCmd;
}

bool CommandManager::IsSerialConnected()
{
    return tud_ready();
}

bool CommandManager::IsTerminalConnected()
{
    return tud_cdc_connected();
}

void CommandManager::InitSerial()
{
    stdio_usb_init();
}

void CommandManager::HandlePrompt()
{
    // Switch to the correct keyboard mode
    SetKeyboardActiveMode(IsKeyboardModeActive());
    
    // Show welcome message?
    if (IsTerminalConnected())
    {
        // Serial connection is established
        // Should we display the user prompt?
        if (!gbPromptDisplayedOnce)
        {
            // Display it only once
            gbPromptDisplayedOnce = true;
            // Give some time to the serial monitor!
            sleep_ms(50);
            // Show prompt and get commands!
            ShowPrompt();
            // Display the correct prompt
            ShowCommandsList();
            // Activate remote keyboard by default
            if (((int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::RemoteKeyboardMode))
            {
                gbKeyboardModeActive = false;
                SetKeyboardActiveMode(true);
            }
            if (!IsKeyboardModeActive())
            {
                ResetCommand();
            }
        }
    }
    else
    {
        // Cancel the previous display of the prompt
        gbPromptDisplayedOnce = false;
        // Nobody to read pending messages
        gcPendingMessages[0] = 0;
    }

    // Display messages received in the background (ROM loaded...)
    if (gbPromptDisplayedOnce)
    {
        DisplayPendingMessages();
    }

    if (IsTerminalConnected() || IsSerialConnected())
    {
        if (IsKeyboardModeActive())
        {
            // Remote keyboard mode enabled, send keys to the 68k
            char c;
            if (GetChar(&c))
            {
                // Tab key?
                if (c == REMOTE_KEYBOARD_ACTIVATION_KEY && ((int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::RemoteKeyboardMode))
                {
                    SetKeyboardActiveMode(false);
                }
                else
                {
                    sleep_ms(1);
                    ROMImageManager::GetImageBaseAddressB()[0x1fffa] = (uint8_t)c;
                    ROMImageManager::GetImageBaseAddressB()[0x1fffb]++;
                }
            }
        }
        else
        {
            // Read commands
            ReadCommands();
        }
    }
}

bool CommandManager::GetChar(char *received)
{
    int result = getchar_timeout_us(10);
    if (result >= 0)
    {
        *received = (char)(result & 0xff);
        return true;
    }
    return false;
}

char CommandManager::GetCharBlocking()
{
    char c;
    while (!GetChar(&c))
    {
        MassStorageManager::Loop();
        ROMUploadManager::Loop();
    }
    return c;
}

void CommandManager::ResetCommand()
{
    gcCurrentCommand[0] = 0;
    if (IsTerminalConnected())
    {
        // Messages received while a command was running are displayed before the new prompt
        FlushPendingMessages();
        ShowInputLine();
    }
}

/// @brief Display the prompt followed by the command being typed
void CommandManager::ShowInputLine()
{
    SetColorForeground(COLOR_INDEX_PROMPT);
    printf("> ");
    SetColorForeground(COLOR_INDEX_NORMAL);
    printf("%s", gcCurrentCommand);
}

void CommandManager::FlushPendingMessages()
{
    if (gcPendingMessages[0] != 0)
    {
        // Take the messages out of the queue before printing, in case USB processing queues a new one meanwhile
        char messages[MAX_LENGTH_PENDING_MESSAGES + 1];
        strcpy(messages, gcPendingMessages);
        gcPendingMessages[0] = 0;

        SetColorForeground(COLOR_INDEX_RETURN_MESSAGES);
        printf("%s", messages);
        SetColorForeground(COLOR_INDEX_NORMAL);
    }
}

/// @brief Queue a message coming from outside the command flow (e.g. ROM received). Safe to call from USB callbacks.
/// It is displayed by HandlePrompt() without breaking the console (input line erased, message displayed, prompt and command being typed restored),
/// or before the next prompt if a command is running.
void CommandManager::PrintAsyncMessage(const char *format, ...)
{
    if (!IsTerminalConnected())
    {
        return;
    }

    char message[MAX_LENGTH_ASYNC_MESSAGE + 1];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Queue the message (dropped if the queue is full)
    size_t pendingLength = strlen(gcPendingMessages);
    if (pendingLength + strlen(message) + 1 <= MAX_LENGTH_PENDING_MESSAGES)
    {
        strcpy(gcPendingMessages + pendingLength, message);
        strcat(gcPendingMessages, "\n");
    }
}

/// @brief Display queued asynchronous messages below the input line, then display the input line again
/// so that the user sees the typing has been interrupted (no prompt in remote keyboard mode)
void CommandManager::DisplayPendingMessages()
{
    if (gcPendingMessages[0] == 0 || gbCommandRunning)
    {
        return;
    }
    if (IsKeyboardModeActive())
    {
        // No input line in remote keyboard mode: just make sure we start at the beginning of an empty line
        printf("\r\e[2K");
        FlushPendingMessages();
    }
    else
    {
        printf("\n");
        FlushPendingMessages();
        ShowInputLine();
    }
}

void CommandManager::ShowPrompt()
{
    printf("\033[2J\033[H");
    SetColorForeground(COLOR_INDEX_NORMAL);
    printf("\n\nWelcome to BricoNeo - Compiled " BUILD_DATE_TIME_FULL " - ");
    SetColorForeground(1);
    printf("Bouz/@AurelienBricole\n");
    SetColorForeground(COLOR_INDEX_NORMAL);
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    printf("SN: %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n", id.id[0], id.id[1], id.id[2], id.id[3], id.id[4], id.id[5], id.id[6], id.id[7]);
    if (BricoExtManager::IsBricoNeoROM())
    {
        printf("Found BricoNeo ROM version %d.%d. Special mode flags: 0x%04x\n", BricoExtManager::GetBricoNeoROMVersion() >> 16, BricoExtManager::GetBricoNeoROMVersion() & 0xffff, (int)BricoExtManager::GetSpecialModeFlags());
    }
    printf("\n");
    SetColorForeground(255);
    printf("    ____       _            _   __         \n");
    SetColorForeground(245);
    printf("   / __ )_____(_)________  / | / /__  ____ \n");
    SetColorForeground(235);
    printf("  / __  / ___/ / ___/ __ \\/  |/ / _ \\/ __ \\\n");
    SetColorForeground(245);
    printf(" / /_/ / /  / / /__/ /_/ / /|  /  __/ /_/ /\n");
    SetColorForeground(255);
    printf("/_____/_/  /_/\\___/\\____/_/ |_/\\___/\\____/ \n");
    printf("\n");
    printf("\n");
}

void CommandManager::ShowCommandsList()
{
    SetColorForeground(COLOR_INDEX_NORMAL);  
    printf("Type ");
    SetColorForeground(COLOR_INDEX_COMMAND_PARAMETERS);
    printf("?");
    SetColorForeground(COLOR_INDEX_NORMAL);
    printf(" to see available commands again.\n");
    printf("Type ");
    SetColorForeground(COLOR_INDEX_COMMAND_PARAMETERS);
    printf("*");
    SetColorForeground(COLOR_INDEX_NORMAL);
    printf(" before command name to loop until a key is pressed.\n");
    printf("Type ");
    SetColorForeground(COLOR_INDEX_COMMAND_PARAMETERS);
    printf("**");
    SetColorForeground(COLOR_INDEX_NORMAL);
    printf(" before command name to loop and refresh the screen before each loop.\n\n");
    printf("Available commands:\n");
    Command::CommandSection eCurrentSection = (Command::CommandSection)-1;
    for (int iCommand = 0;;++iCommand)
    {
        tud_task();
        Command *pCommand = GetCommand(iCommand);
        if (pCommand != NULL)
        {
            char buffer[MAX(COMMAND_MAX_LENGTH_NAME, MAX(COMMAND_MAX_LENGTH_DESC_PARAMETERS, MAX(COMMAND_MAX_LENGTH_DESC_TEXT, COMMAND_MAX_LENGTH_SECTION_DESCRIPTION + COMMAND_MAX_LENGTH_SECTION_NAME + 1))) + 1];

            // Section management
            if (pCommand->GetCommandSection() != eCurrentSection)
            {
                // Command section changed
                eCurrentSection = pCommand->GetCommandSection();
                pCommand->GetSectionInfo(eCurrentSection, buffer, buffer + COMMAND_MAX_LENGTH_SECTION_NAME + 1);
                printf("\n");
                SetColorForeground(COLOR_INDEX_SECTION_NAME);
                printf(buffer);
                SetColorForeground(COLOR_INDEX_NORMAL);
                printf(":\n");
                printf(buffer + COMMAND_MAX_LENGTH_SECTION_NAME + 1);
                printf("\n");
            }

            // Command name
            pCommand->GetCommandName(buffer);
            SetColorForeground(COLOR_INDEX_COMMAND_NAME);
            printf("- %s", buffer);

            // Command parameters
            pCommand->GetCommandDescriptionParameters(buffer);
            if (buffer[0] != 0)
            {
                SetColorForeground(COLOR_INDEX_COMMAND_PARAMETERS);
                printf(" %s", buffer);
            }

            // Command description
            pCommand->GetCommandDescriptionText(buffer);
            if (buffer[0] != 0)
            {
                SetColorForeground(COLOR_INDEX_COMMAND_DESCRIPTION);
                printf(" => %s", buffer);
            }

            // Done with this command
            printf("\n");

            // Dont' forget to delete the command
            delete pCommand;
        }
        else
        {
            // No more commands to show
            break;
        }
    }
    if ((int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::RemoteKeyboardMode)
    {
        printf("\n");
        SetColorForeground(COLOR_INDEX_SECTION_NAME);
        printf("Remote keyboard");
        SetColorForeground(COLOR_INDEX_NORMAL);
        printf(":\nThis ROM supports remote keyboard. Press the TAB key to switch between ");
        SetColorForeground(COLOR_INDEX_KEYBOARD_MODE_COMMAND);
        printf("command");
        SetColorForeground(COLOR_INDEX_NORMAL);
        printf(" and ");
        SetColorForeground(COLOR_INDEX_KEYBOARD_MODE_REMOTE);
        printf("remote");
        SetColorForeground(COLOR_INDEX_NORMAL);
        printf(" keyboard modes\n");
    }
    printf("\n");
}

void CommandManager::ReadCommands()
{
    char cReceived;
    if (GetChar(&cReceived))
    {
        // A character has been received
        // Filter it
        //printf("Received character 0x%02x\n", (int)gcCurrentCommand[0]);
        bool bDel = false;
        bool bReturn = false;
        bool bIgnore = false;
        bool bHistory = false;
        char cTyped = 0;

        // Backspace: 0x7f (or 0x08, depending on the terminal settings, e.g. Tera Term)
        // Home:    0x1b 0x5b 0x31 0x7e
        // DEL:     0x1b 0x5b 0x33 0x7e
        // Up:      0x1b 0x5b 0x41
        // Down:    0x1b 0x5b 0x42
        // Right:   0x1b 0x5b 0x43
        // Left:    0x1b 0x5b 0x44
        // Enter/Return: 0x0d

        int currentLength = strlen(gcCurrentCommand);

        switch (cReceived)
        {
        case ' ':
            if (currentLength == 0)
            {
                bIgnore = true;
            }
            else
            {
                cTyped = cReceived;
            }
        case 0x0a:
        case 0x0d:
            bReturn = true;
            break;
        case 0x7f:
        case 0x08:
            bDel = true;
            break;
        case REMOTE_KEYBOARD_ACTIVATION_KEY: // Tab
            if ((int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::RemoteKeyboardMode)
            {
                SetKeyboardActiveMode(true);
                bIgnore = true;
            }
            break;
        case 0xff:
            bIgnore = true;
            break;
        case 0x1b:
            switch (GetCharBlocking())
            {
            case 0x5b:
                switch (GetCharBlocking())
                {
                case 0x31:
                    // Home
                    GetCharBlocking();
                    for (int i = 0; i < currentLength; ++i)
                    {
                        putchar_raw(0x08); // Backspace
                        putchar_raw(' ');
                        putchar_raw(0x08); // Backspace
                    }
                    gcCurrentCommand[0] = 0;
                    break;
                case 0x33:
                    // DEL
                    GetCharBlocking();
                    bDel =true;
                    break;
                case 0x41:
                case 0x42:
                    // Up arrow
                    // Down arrow
                    bHistory = true;
                    break;
                case 0x44:
                    // Left arrow
                    bDel = true;
                    break;
                default:
                    // Other arrows
                    bIgnore = true;
                }
                break;
            default:
                bIgnore = true;
            }
            break;
        default:
            if ((unsigned char)cReceived < 0x20)
            {
                // Other control characters would break the input line display
                bIgnore = true;
            }
            else
            {
                cTyped = cReceived;
            }
            break;
        }

        if (!bIgnore)
        {
            if (bHistory)
            {
                // Ensure we have a previous command
                if (gcPreviousCommand[0] == 0)
                {
                    // No history
                    putchar_raw(0x07); // Bell
                }
                else
                {
                    // Erase current command
                    for (;currentLength > 0; --currentLength)
                    {
                        putchar_raw(0x08); // Backspace
                        putchar_raw(' ');
                        putchar_raw(0x08); // Backspace
                    }
                    strcpy(gcCurrentCommand, gcPreviousCommand);
                    currentLength = strlen(gcCurrentCommand);
                    printf(gcCurrentCommand);
                }
            }
            if (bDel)
            {
                if (currentLength > 0)
                {
                    gcCurrentCommand[currentLength - 1] = 0;
                    putchar_raw(0x08); // Backspace
                    putchar_raw(' ');
                    putchar_raw(0x08); // Backspace
                }
                else
                {
                    putchar_raw(0x07); // Bell
                }
            }
            else if (cTyped > 0)
            {
                // Ensure the text still fits in the input buffer
                if (currentLength < MAX_COMMAND_WIDTH)
                {
                    gcCurrentCommand[currentLength++] = cTyped;
                    gcCurrentCommand[currentLength] = 0;
                    if (IsTerminalConnected())
                    {
                        putchar_raw(cTyped);
                    }
                }
                else
                {
                    putchar_raw(7); // Bell
                }
            }
            else if (bReturn)
            {
                // Validate command

                // Display command list again?
                if (strcmp(gcCurrentCommand, "?") == 0)
                {
                    printf("\n\n");
                    ShowCommandsList();
                    ResetCommand();
                }
                else
                {
                    // Dirty: handle comments colorization after they have been validated
                    if (strncmp(gcCurrentCommand, COMMAND_NAME_COMMENT, strlen(COMMAND_NAME_COMMENT)) == 0)
                    {
                        // Erase current command
                        int length = strlen(gcCurrentCommand);
                        for (;length > 0; --length)
                        {
                            putchar_raw(0x08); // Backspace
                        }
                        SetColorForeground(COLOR_INDEX_COMMENT);
                        printf(gcCurrentCommand);
                        SetColorForeground(COLOR_INDEX_NORMAL);
                    }

                    if (IsTerminalConnected())
                    {
                        printf("\n");
                    }
                    gbCommandRunning = true;
                    CommandRequest();
                    gbCommandRunning = false;
                }
            }
        }
    }
}

void CommandManager::CommandRequest()
{
    // Backup the command to allow history
    if (gcCurrentCommand[0] != 0)
    {
        strcpy(gcPreviousCommand, gcCurrentCommand);
    }

    // Parse the current command to get its name
    char *pCommandStart = gcCurrentCommand;

    // Determine if the command starts with a star
    bool bAutoRepeat = (pCommandStart[0] == '*');
    if (bAutoRepeat)
    {
        // Skip the star
        ++pCommandStart;
    }
    // Determine if the command starts with 2 stars
    bool bAutoRepeatRefresh = (pCommandStart[0] == '*');
    if (bAutoRepeatRefresh)
    {
        // Skip the star
        ++pCommandStart;
    }

    bool bOutputBinaryMode = (pCommandStart[0] == '@');
    if (bOutputBinaryMode)
    {
        // Skip the marker
        ++pCommandStart;
    }

    if (bAutoRepeat &&! IsTerminalConnected())
    {
        SetColorForeground(COLOR_INDEX_ERROR);
        printf("Comand loop not possible in non interactive mode.\n");
        return;
    }

    // Input command looks like "LoADbAnk 123"
    char *pcCommandName = NULL;
    char *pcParam1 = NULL;
    char *pcParam2 = NULL;
    char *pcParam3 = NULL;
    char *pcParam4 = NULL;

    // Tokenize the command
    if ((pcCommandName = strtok(pCommandStart, " ")) != NULL)
        if ((pcParam1 = strtok(NULL, " ")) != NULL)
            if ((pcParam2 = strtok(NULL, " ")) != NULL)
                if ((pcParam3 = strtok(NULL, " ")) != NULL)
                    pcParam4 = strtok(NULL, " ");

    // Do we have a valid command name?
    if (pcCommandName != NULL)
    {
        // Find the corresponding command!
        for (int iCommand = 0;;iCommand++)
        {
            Command *cmd = GetCommand(iCommand);
            if (cmd != NULL)
            {
                // Command name
                char pcCurrentName[COMMAND_MAX_LENGTH_NAME + 1];
                cmd->GetCommandName(pcCurrentName);
                // Same name? (case insensitive)
                if (strcasecmp(pcCurrentName, pcCommandName) == 0)
                {
                    // We found the command
                    // Check parameters count
                    int expectedParamCount = cmd->GetParametersCount();
                    bool bParamCountOK = false;
                    if (cmd->IsParametersCountCheckEnabled())
                    {
                        if (pcParam1 == NULL)
                        {
                            if (expectedParamCount == 0)
                                bParamCountOK = true;
                        }
                        else if (pcParam2 == NULL)
                        {
                            if (expectedParamCount == 1)
                                bParamCountOK = true;
                        }
                        else if (pcParam3 == NULL)
                        {
                            if (expectedParamCount == 2)
                                bParamCountOK = true;
                        }
                        else if (pcParam4 == NULL)
                        {
                            if (expectedParamCount == 3)
                                bParamCountOK = true;
                        }
                    }
                    else
                    {
                        // No need to check parameters count
                        bParamCountOK = true;
                    }
                    if (bParamCountOK)
                    {
                        // Set additional command parameters
                        cmd->SetOutputBinaryMode(bOutputBinaryMode);

                        // Try to execute the command
                        char pcReturnMessage[MAX_LENGTH_RETURN_MESSAGE + 1] = "";
                        pcReturnMessage[0] = 0;
                        bool bResult = true;
                        bool bResultChanged = true;
                        uint32_t nAutoRepeatLoopCount = 0;
                        uint32_t previousCommandDataCRC = 0xffffffff;
                        cmd->SetLastCommandDataCRC(previousCommandDataCRC);
                        cmd->SetLoopMode(bAutoRepeatRefresh);
                        char c;
                        if (IsTerminalConnected())
                        {
                            SetColorForeground(COLOR_INDEX_RETURN_MESSAGES);
                        }
                        while (bResult & !(bAutoRepeat && GetChar(&c)))
                        {
                            tud_task();
                            bResult = cmd-> Execute(pcReturnMessage, MAX_LENGTH_RETURN_MESSAGE, pcParam1, pcParam2, pcParam3);
                            
                            if (IsTerminalConnected())
                            {
                                if (!bResult)
                                {
                                    SetColorForeground(COLOR_INDEX_ERROR);
                                    printf("The command returned an error.\n");
                                }
                                // Display return value if any
                                // Check if the result changed
                                if (bResultChanged = (previousCommandDataCRC != cmd->GetLastCommandDataCRC()))
                                {
                                    // Result changed
                                    // Update CRC
                                    previousCommandDataCRC = cmd->GetLastCommandDataCRC();
                                    ++nAutoRepeatLoopCount;
                                    cmd->SetLoopCount(nAutoRepeatLoopCount);
                                }
                                if (pcReturnMessage[0] != 0 && (!bAutoRepeatRefresh || bResultChanged))
                                {
                                    if (bAutoRepeatRefresh)
                                    {
                                        CommandManager::HideCursor();
                                        CommandManager::ClearTerminal();
                                        printf("Loop count: %d\n\n", nAutoRepeatLoopCount);
                                    }
                                    printf(pcReturnMessage);
                                    printf("\n");
                                    if (bAutoRepeatRefresh)
                                    {
                                        CommandManager::ShowCursor();
                                    }
                                }
                                // Switch back to normal color
                            }

                            if (bAutoRepeat)
                            {
                                // Do USB processing to handle serial line, mass storage and ROM upload events
                                MassStorageManager::Loop();
                                ROMUploadManager::Loop();
                            }
                            else
                            {
                                // Execute only once in non auto-repeat mode
                                break;
                            }
                        }
                        if (IsTerminalConnected())
                        {
                            SetColorForeground(COLOR_INDEX_NORMAL);
                        }
                    }
                    else
                    {
                        // Wrong parameters count
                        SetColorForeground(COLOR_INDEX_ERROR);
                        printf("Wrong parameters count. Expected %d\n", expectedParamCount);
                        SetColorForeground(COLOR_INDEX_NORMAL);
                    }
                    // Stop searching commands
                    if (cmd != NULL)
                    {
                        delete cmd;
                        cmd = NULL;
                    }
                    break;
                }

                // Delete the command
                    if (cmd != NULL)
                    {
                        delete cmd;
                        cmd = NULL;
                    }
            }
            else
            {
                // No more commands
                SetColorForeground(COLOR_INDEX_ERROR);
                printf("Unknown command.\n");
                break;
            }
            
        }
    }
    ResetCommand();
}

void CommandManager::SetKeyboardActiveMode(bool active)
{
    bool bSerialReady = IsTerminalConnected() || IsSerialConnected();
    
    if (bSerialReady)
    {
        // Check if keyboard is supported
        bool bRomSupportsKeyboard = (int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::RemoteKeyboardMode;
        
        // Serial connection is ready
        if (bRomSupportsKeyboard)
        {
            // Active flag changed?
            if (gbKeyboardModeActive != active)
            {
                // Store new activity value
                gbKeyboardModeActive = active;
                if (active)
                {
                    SetColorForeground(COLOR_INDEX_KEYBOARD_MODE_REMOTE);
                    printf("\nRemote keyboard mode. Keys are tranfered to the remote (type TAB to get back to command mode)\n");
                    SetColorForeground(COLOR_INDEX_NORMAL);
                    ActivityLED::SetActivityType(ActivityLED::ActivityType::RemoteKeyboardConnected);

                }
                else
                {
                    SetColorForeground(COLOR_INDEX_KEYBOARD_MODE_COMMAND);
                    printf("\nCommand mode (type TAB to get back to remote keyboard mode)\n");
                    SetColorForeground(COLOR_INDEX_NORMAL);
                    ResetCommand();
                    ActivityLED::SetActivityType(IsTerminalConnected() ? ActivityLED::ActivityType::TerminalConnected : ActivityLED::ActivityType::Idle);
                }
            }
        }
        else
        {
            // Keyboard not supported
            ActivityLED::SetActivityType(IsTerminalConnected() ? ActivityLED::ActivityType::TerminalConnected : ActivityLED::ActivityType::Idle);
        }
    }
    else
    {
        // Serial connection is not ready
        // Disable keywbord
        gbKeyboardModeActive = false;
        ActivityLED::SetActivityType(ActivityLED::ActivityType::Idle);
    }
}
