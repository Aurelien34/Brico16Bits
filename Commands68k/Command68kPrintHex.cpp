#include "Command68kPrintHex.h"

#define COMMAND_NAME "PRHEX"

void Command68kPrintHex::GetName(char *name) const
{
    strncpy(name, COMMAND_NAME, COMMAND68K_NAME_LENGTH);
}

void Command68kPrintHex::Execute()
{
    // The first word should be the number of digits to display
    // Next word is the lower word of the number to display
    // Next word, if any (digit count >= 4) is the high word of the number to display
    
    uint16_t digitCount;
    if (InputBuffer::GetValue(&digitCount))
    {
        // Check input parameter
        if (digitCount > 0 && digitCount <= 8)
        {
            // Now load the digits
            uint16_t valueLow,valueHigh;
            if (InputBuffer::GetValue(&valueLow))
            {
                uint16_t digitCount1 = (digitCount > 4 ? 4 : digitCount);
                uint16_t digitCount2 = (digitCount > 4 ? digitCount - 4 : 0);

                if (digitCount2 > 0)
                {
                    if (!InputBuffer::GetValue(&valueHigh))
                    {
                        valueHigh = 0;
                    }
                    for (int i = 0; i < digitCount2; ++i)
                    {
                        printf("%x", (valueHigh >> ((digitCount2 - 1 - i) * 4) ) & 0x0f);
                    }
                }

                for (int i = 0; i < digitCount1; ++i)
                {
                    printf("%x", (valueLow >> ((digitCount1 - 1 - i) * 4) ) & 0x0f);
                }

                printf("\n");
            }
        }
    }
}
