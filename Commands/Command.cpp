#include <stdlib.h>
#include "Command.h"
#include "ROMImageManager.h"

void Command::GetCommandDescriptionParameters(char *parameters) const
{
    parameters[0] = 0;
}

void Command::GetCommandDescriptionText(char *text) const
{
    text[0] = 0;
}

int Command::GetParametersCount() const
{
    char *ptr;
    char parameters[COMMAND_MAX_LENGTH_DESC_PARAMETERS + 1];
    ptr = parameters;
    GetCommandDescriptionParameters(parameters);
    int count = 0;
    if (*ptr != 0)
    {
        count = 1;
    }
    while(*ptr) if (*ptr++ == ' ') ++count;
    return count;
}

void Command::GetSectionInfo(Command::CommandSection section, char *sectionName, char *sectionDescription) const
{
    switch (section)
    {
    case CommandSection::Misc:
        snprintf(sectionName, COMMAND_MAX_LENGTH_SECTION_NAME, "Miscellaneous commands");
        snprintf(sectionDescription, COMMAND_MAX_LENGTH_SECTION_DESCRIPTION, "Various more or less useful commands");
        break;
    case CommandSection::ROM:
        snprintf(sectionName, COMMAND_MAX_LENGTH_SECTION_NAME, "ROM images management");
        snprintf(sectionDescription, COMMAND_MAX_LENGTH_SECTION_DESCRIPTION, "ROM images can be stored in %d flash memory banks.\nBank #%d is automatically loaded at startup", FLASH_MAX_ROM_BANKS, FLASH_ROM_BANK_DEFAULT);
        break;
    case CommandSection::Buffer:
        snprintf(sectionName, COMMAND_MAX_LENGTH_SECTION_NAME, "BricoNeo buffer access");
        snprintf(sectionDescription, COMMAND_MAX_LENGTH_SECTION_DESCRIPTION, "Get 16 bits words sent by the NeoGeo to the BricoNeo buffer");
        break;
    case CommandSection::System:
        snprintf(sectionName, COMMAND_MAX_LENGTH_SECTION_NAME, "System");
        snprintf(sectionDescription, COMMAND_MAX_LENGTH_SECTION_DESCRIPTION, "System functions");
        break;
    case CommandSection::BricoExt:
        snprintf(sectionName, COMMAND_MAX_LENGTH_SECTION_NAME, "Extensions");
        snprintf(sectionDescription, COMMAND_MAX_LENGTH_SECTION_DESCRIPTION, "Functions available with currently loaded BricoNeo ROM. Parameters can be either decimal or hexadecimal integers");
        break;
    default:
        sectionName[0] = 0;
        sectionDescription[0] = 0;
    }
}

uint32_t Command::ParseUint(const char *number)
{
    uint32_t result = 0;
    if (number[0] == '$')
    {
        // Hexadecimal value
        // Skip "$" as it is not recognized by scanf as an hexadecimal marker
        sscanf(number+1, "%x",&result);
    }
    else if (number[0] == '0' && number[1] == 'x')
    {
        // Hexadecimal value
        sscanf(number, "%x", &result);
    }
    else
    {
        // Decimal value
        sscanf(number, "%d", &result);
    }
    return result;
}
