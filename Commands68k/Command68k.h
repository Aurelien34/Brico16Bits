#pragma once

#include <stdio.h>
#include <string.h>
#include "InputBuffer.h"

#define COMMAND68K_NAME_LENGTH 5

class Command68k
{
public:
    virtual void GetName(char *buffer) const = 0;
    virtual void Execute() = 0;
};