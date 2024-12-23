#pragma once

#include "Command.h"

#define COMMAND_NAME_COMMENT ";"

class CommandComment : public Command
{
public:
    virtual CommandSection GetCommandSection() const { return CommandSection::Misc; };
    virtual void GetCommandName(char *name) const;
    virtual void GetCommandDescriptionText(char *text) const;
    virtual bool Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3);
    virtual bool IsParametersCountCheckEnabled() { return false; }
};