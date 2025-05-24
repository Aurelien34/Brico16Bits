#pragma once

#include "Command68k.h"

class Command68kSwitchBank : public Command68k
{
public:
    virtual void GetName(char *name) const;
    virtual void Execute();
};