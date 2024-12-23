#pragma once

#include "Command68k.h"

class Command68kPrintHex : public Command68k
{
public:
    virtual void GetName(char *name) const;
    virtual void Execute();
};