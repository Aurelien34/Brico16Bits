#pragma once

#include "Command.h"
#include "DebugTuiView.h"
#include "DebugLink.h"
#include "CallStack.h"

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
        StepOver,       // 'o' / F10
        StepOut,        // 'u'
        RunTo,          // 'g': run to an address
        Break,          // 'b': request a break while running
        Continue,
        Exit,
        ToggleView,
        Redraw,
        Resize,
        Memory,         // 'm': enter a memory address
        ToggleWords,    // 'w': 16-bit words / bytes
        Write,          // 'e': write memory (prompt)
        EditMode,       // 'i': edit memory at a cursor (full screen view, memory panel)
        ToggleCode,    // F3: code / memory panel
        PageUp,
        PageDown,
        LineUp,
        LineDown,
        CursorLeft,     // From here: only returned by ReadAction(bEditMode = true), memory panel edit mode (T14)
        CursorRight,
        EditDigit,
        EditEnter,
        EditBackspace,
        EditEscape,
    };

    static Action ReadAction(bool bEditMode = false, unsigned *pDigit = nullptr);
    static void ServiceBackgroundTasks(DebugTuiView &view);
    void ToggleView(DebugTuiView &view, const DebugFrame *pFrame, bool bWaitingForCommand);
    struct WaitContext;
    static bool PollWaiting(void *context);
    static void ReportLinkError(DebugTuiView &view, char *returnMessage, int messageMaxSize, const char *what, const DebugLink &link);
    void ShowFrameClassic(const DebugFrame *frame);
    /// @brief Classic view: one line of the Program box ("| -> " / "|    " prefix, address, text, words)
    void PrintInstructionClassic(const DisassembledInstruction *instruction, const char *prefix);

    /// @brief What the 68k was asked to do before the current stop
    enum class ResumeKind
    {
        None,       ///< First stop of the session
        Step,       ///< STEP
        RunUntil,   ///< RUN_UNTIL (step over or run to)
        Continue,   ///< CONTINUE
    };

    /// @brief How the current stop was reached, for T10 (call stack: CallStack::OnStep / OnRunUntilReached / Invalidate).
    /// Valid from the moment the stop is decoded until the next command. Updated in Execute() right before the history.
    struct StopInfo
    {
        ResumeKind kind = ResumeKind::None;
        bool bStepOver = false;         ///< kind == RunUntil: it was a step over or a step out (not a 'g')
        bool bStepOut = false;          ///< kind == RunUntil && bStepOver: it was a step out (history is not fed)
        uint32_t runUntilTarget = 0;    ///< kind == RunUntil: PC argument
        uint32_t runUntilMinSp = 0;     ///< kind == RunUntil: minimum SP argument (the 68k compares SSP before the exception)
        /// kind == RunUntil: stopped on the target (PC == target and SSP >= minimum SP). false: break, or another
        /// breakpoint / trace event, the call stack must be invalidated.
        bool bReachedTarget = false;
    };
    StopInfo mStop;

    /// @brief Instructions executed before the current stop (see ExecutionHistory). Cleared at the start of Dbg, after
    /// CONTINUE and on any DebugLink error. T8 / T10: GetPrevious() gives the previous PC, SP (effective a7) and size.
    ExecutionHistory mHistory;

    /// @brief Call stack analyzer (shadow stack kept across stops, T10). One instance, reset at the start of Dbg.
    CallStack mCallStack;
    /// @brief Callers of the current stop (innermost first), as displayed and used by step out
    Caller mCallers[DEBUG_CALLERS_MAX_LINES];
    int mCallerCount = 0;
    CallersStatus mCallersStatus = CallersStatus::Unknown;
    void ShowSplitter();
    void RepeatChars(char c, int16_t count);
};