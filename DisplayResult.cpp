#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "DisplayResult.h"
#include "InputBuffer.h"

#define DUMP_DISPLAY_LINE_WORDS_COUNT 8

void DisplayResult::Show(DisplayResult::ResultType eResultType, char *returnMessage, int messageMaxSize, bool binaryMode)
{
    switch (eResultType)
    {
    case ResultType::Void:
        // Nothing to display
        break;
    case ResultType::SingleWord:
        DisplaySingleWord();
        break;
    case ResultType::String:
        DisplayString();
        break;
    case ResultType::Dump:
        DisplayDump(binaryMode);
        break;
    default:
        printf("Wrong return type sent by host (%d)\n", (int)eResultType);
        break;
    }
}

void DisplayResult::DisplaySingleWord()
{
    // Read a single word
    uint16_t result;
    if (InputBuffer::GetValue(&result))
    {
        printf("0x%04x\n", result);
    }
    else
    {
        printf("[Empty]\n");
    }
}

void DisplayResult::DisplayString()
{
    // Read a string
    // Characters will be printed one by one to avoid reserving a result string in memory!
    uint16_t result;
    uint32_t uCRC = (uint32_t)(-1L);
    while (InputBuffer::GetValue(&result))
    {
        // Send the character to the host
        char character = (char)result;
        // Don't send it if it is a terminating 0
        // The string should be terminated by the EOF signal
        if (character != 0)
        {
            putchar((int)character);
        }
    }
    printf("\n");
}

void DisplayResult::DisplayDump(bool binaryMode)
{
    // Read a memory dump. Start address (2 words), then a list of words
    uint16_t result;
    uint32_t baseAddress = 0;
    uint32_t uCRC = (uint32_t)(-1L);
    if(InputBuffer::GetValue(&result))
    {
        baseAddress = (uint32_t)result;
    }
    if(InputBuffer::GetValue(&result))
    {
        baseAddress |= (((uint32_t)result) << 16);
    }
    if (!binaryMode)
    {
        printf("Memory content starting at address 0x%06x:\n", baseAddress);
    }

    // Reserve space for one line
    char wordLineToDisplay[DUMP_DISPLAY_LINE_WORDS_COUNT * (4 + 1) + 1];
    char *wordNextCharToSet = &wordLineToDisplay[0];
    wordLineToDisplay[0] = 0;
    char charLineToDisplay[DUMP_DISPLAY_LINE_WORDS_COUNT * 2 + 1];
    char *charNextCharToSet = &charLineToDisplay[0];
    charLineToDisplay[0] = 0;
    bool bContinue = true;
    int lastWordCount;

    if (binaryMode)
    {
        // Binaty mode
        while (InputBuffer::GetValue(&result))
        {
            stdio_put_string((const char *)&result, 2, false, false);
        }
    }
    else
    {
        // Terminal / Text mode
        while (bContinue)
        {
            lastWordCount = 0;
            int wrd = 0;
            for (; wrd < DUMP_DISPLAY_LINE_WORDS_COUNT & bContinue; ++wrd)
            {
                if (InputBuffer::GetValue(&result))
                {
                    sprintf(wordNextCharToSet, "%04x ", result);
                    wordNextCharToSet += 5;

                    char c = (char)(result & 0xff);
                    charNextCharToSet[1] = ((c >= 32 && c < 128) ? c : '.' );
                    c = (char)((result >> 8)& 0xff);
                    charNextCharToSet[0] = ((c >= 32 && c < 128) ? c : '.' );
                    charNextCharToSet[2] = 0;
                    charNextCharToSet += 2;

                    ++lastWordCount;
                }
                else
                {
                    bContinue = false;
                }
            }

            // Something to print?
            if (lastWordCount > 0)
            {
                // Print the address
                printf("%06x: ", baseAddress);

                // Print the words
                printf(wordLineToDisplay);

                // Stuff for potential missing parity words
                if (!bContinue)
                {
                    if (lastWordCount != DUMP_DISPLAY_LINE_WORDS_COUNT)
                    {
                        for (int i = lastWordCount; i < DUMP_DISPLAY_LINE_WORDS_COUNT; ++i)
                        {
                            printf("     ");
                        }
                    }
                }

                printf(" | ");
                printf(charLineToDisplay);
                printf("\n");

                if (bContinue)
                {
                    // Rewind the line
                    wordNextCharToSet = &wordLineToDisplay[0];
                    wordLineToDisplay[0] = 0;
                    charNextCharToSet = &charLineToDisplay[0];
                    charLineToDisplay[0] = 0;

                    // Next address ot display
                    baseAddress += 2 * DUMP_DISPLAY_LINE_WORDS_COUNT;
                }
            }
        }

        printf("\n");
    }
}
