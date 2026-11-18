#pragma once

#include "Command.h"
#include "DebugTuiView.h"
#include "DebugLink.h"

class CommandDebug : public Command
{
public:
    virtual CommandSection GetCommandSection() const { return CommandSection::Misc; };
    virtual void GetCommandName(char *name) const;
    virtual void GetCommandDescriptionParameters(char *parameters) const;
    virtual void GetCommandDescriptionText(char *text) const;
    virtual bool IsParametersCountCheckEnabled() { return false; }
    virtual bool Execute(char *returnMessage, int messageMaxSize, const char *param1, const char *param2, const char *param3);
private:
    enum class Action
    {
        None,
        Step,
        Continue,
        Exit,
        ToggleView,
        Redraw,
        Resize,
        Memory,         // 'm': enter a memory address
        ToggleWords,    // 'w': 16-bit words / bytes
        ToggleCode,     // F3: code / memory panel
        PageUp,
        PageDown,
        LineUp,
        LineDown,
    };

    static Action ReadAction();
    static void ServiceBackgroundTasks(DebugTuiView &view);
    void ToggleView(DebugTuiView &view, const DebugFrame *pFrame, bool bWaitingForCommand);
    struct WaitContext;
    static bool PollWaiting(void *context);
    static void ReportLinkError(DebugTuiView &view, char *returnMessage, int messageMaxSize, const char *what, const DebugLink &link);
    void ShowFrameClassic(const DebugFrame *frame);
    void ShowSplitter();
    void RepeatChars(char c, int16_t count);
};