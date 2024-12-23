#pragma once

#include "Command.h"

class CommandReadWord : public Command
{
public:
    virtual CommandSection GetCommandSection() const { return CommandSection::Buffer; };
    virtual void GetCommandName(char *name) const;
    virtual void GetCommandDescriptionText(char *text) const;
    virtual bool Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3);
};