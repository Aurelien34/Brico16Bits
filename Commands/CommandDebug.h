#pragma once

#include "Command.h"

class CommandDebug : public Command
{
public:
    virtual CommandSection GetCommandSection() const { return CommandSection::Misc; };
    virtual void GetCommandName(char *name) const;
    virtual void GetCommandDescriptionText(char *text) const;
    virtual bool Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3);
private:
    void ShowSplitter();
    void RepeatChars(char c, int16_t count);
    static uint16_t gwTraceRoutine[87];
};