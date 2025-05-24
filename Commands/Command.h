#pragma once

#include <stdio.h>
#include <cstdint>

#define COMMAND_MAX_LENGTH_NAME 15
#define COMMAND_MAX_LENGTH_DESC_PARAMETERS 50
#define COMMAND_MAX_LENGTH_DESC_TEXT 300
#define COMMAND_MAX_LENGTH_SECTION_NAME 500
#define COMMAND_MAX_LENGTH_SECTION_DESCRIPTION 500

class Command
{
public:
    enum class CommandSection : int
    {
        Misc,
        ROM,
        Buffer,
        System,
        BricoExt
    };

public:
    void SetLoopCount(uint32_t loopCount) { this->loopCount = loopCount; }
    inline void SetLastCommandDataCRC(uint32_t lastCRC) { lastCommandDataCRC = lastCRC; }
    inline void SetLoopMode(bool isLoopMode) { loopMode = isLoopMode; }
    inline bool IsLoopMode() const { return loopMode; }
    inline void SetOutputBinaryMode(bool binaryMode) { outputBinaryMode = binaryMode; }
    virtual uint32_t GetLastCommandDataCRC() const { return lastCommandDataCRC; }
    virtual bool IsParametersCountCheckEnabled() { return true; }
    virtual CommandSection GetCommandSection() const = 0;
    virtual void GetCommandName(char *name) const = 0;
    virtual void GetCommandDescriptionParameters(char *parameters) const;
    virtual void GetCommandDescriptionText(char *text) const;
    int GetParametersCount() const;
    virtual bool Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3) = 0;
    void GetSectionInfo(CommandSection section, char *sectionName, char *sectionDescription) const;
protected:
    static uint32_t ParseUint(const char *number);
    uint32_t loopCount;
    bool outputBinaryMode;
private:
    uint32_t lastCommandDataCRC;
    bool loopMode;
};