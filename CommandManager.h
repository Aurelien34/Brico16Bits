#pragma once

class CommandManager
{
public:
    static void InitSerial();
    static bool IsSerialConnected();
    static bool IsTerminalConnected();
    static void HandlePrompt();
    static class Command *GetCommand(int commandIndex);
    static inline void SetColorForeground(unsigned char colorCode) { printf("\e[38;5;%dm", colorCode); }
    static inline void SetColorBackground(unsigned char colorCode) { printf("\e[48;5;%dm", colorCode); }
    static inline void ClearTerminal() { printf("\33[H\33[2J"); }
    static inline void ShowCursor() { printf("\e[?25h"); }
    static inline void HideCursor() { printf("\e[?25l"); }
    static bool GetChar(char *received);
    static char GetCharBlocking();
private:
    static Command * (* gaBuiltInCommandConstructors [])();
    static bool gbPromptDisplayedOnce;
    static char gcPreviousCommand[];
    static char gcCurrentCommand[];
    static void ShowPrompt();
    static void ShowCommandsList();
    static void ReadCommands();
    static void ResetCommand();
    static void CommandRequest();
};