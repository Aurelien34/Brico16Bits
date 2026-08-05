#include "Command68kPrintText.h"

#define COMMAND_NAME "PRTXT"

void Command68kPrintText::GetName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND68K_NAME_LENGTH);
}

void Command68kPrintText::Execute()
{
    // Consider the input buffer as characters to be displayed (one word per character)
    uint16_t value;
    while (InputBuffer::GetValue(&value))
    {
        // Temporary, need an indirection here to handle an external string
        putchar(value);
    }
}
