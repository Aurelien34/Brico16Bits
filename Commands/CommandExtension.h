#pragma once

#include "Command.h"

class CommandExtension : public Command
{
public:
    CommandExtension(int commandIndex);
    virtual CommandSection GetCommandSection() const { return CommandSection::BricoExt; };
    virtual bool IsParametersCountCheckEnabled() { return false; }
    virtual void GetCommandName(char *name) const;
    virtual void GetCommandDescriptionText(char *text) const;
    virtual bool Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3);
private:
    int mnCommandIndex;
};