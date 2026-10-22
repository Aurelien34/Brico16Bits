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
    static void PrintAsyncMessage(const char *format, ...);
private:
    static Command * (* gaBuiltInCommandConstructors [])();
    static bool gbPromptDisplayedOnce;
    static bool gbKeyboardModeActive;
    static bool gbKeyboardModeSerialReady;
    static bool gbCommandRunning;
    static char gcPendingMessages[];
    static char gcPreviousCommand[];
    static char gcCurrentCommand[];
    static void ShowPrompt();
    static void ShowCommandsList();
    static void ReadCommands();
    static void ResetCommand();
    static void ShowInputLine();
    static void FlushPendingMessages();
    static void DisplayPendingMessages();
    static void CommandRequest();
    static inline bool IsKeyboardModeActive() { return gbKeyboardModeActive; }
    static void SetKeyboardActiveMode(bool active);
};