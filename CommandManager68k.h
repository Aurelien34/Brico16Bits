#pragma once

class CommandManager68k
{
public:
    static void HandleCommands();
    static class Command68k * GetCommandByName(char * name);
private:
    static class Command68k *GetCommand(int commandIndex);
    static class Command68k * (* gaBuiltInCommandConstructors [])();
};