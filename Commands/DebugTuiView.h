#pragma once

#include <cstdint>

#include "DebugFrame.h"
#include "MemoryDump.h"

/// @brief An instruction that was really executed (first instruction of a previous stop), kept with the context
/// needed to display it and to feed the call stack analysis (T8 / T10)
struct ExecutedInstruction
{
    DisassembledInstruction instruction;    ///< PC, text and words as disassembled at the stop where it was about to run
    uint32_t sp;                            ///< Effective a7 at that stop (before the instruction ran)
    bool forceBreak;                        ///< The flow is considered broken after it, even if the next PC is contiguous

    /// @brief Size in bytes (wordCount * 2). Only meaningful if IsSizeReliable().
    uint32_t GetSizeBytes() const { return (uint32_t)instruction.wordCount * 2; }
    /// @brief false if the instruction was not decoded ("Unknown") or truncated: its size cannot be trusted
    bool IsSizeReliable() const;
    /// @brief true if the instruction at nextPc follows it directly (reliable size, no forced break)
    bool IsContiguousTo(uint32_t nextPc) const;
};

/// @brief One line shown above the current instruction
struct HistoryRow
{
    const ExecutedInstruction *pInstruction;
    bool isFromLine;    ///< true: the flow was broken, the line says where we come from ("<- addr"); false: contiguous
};

/// @brief The last executed instructions (oldest first), at most MAX_ENTRIES. Cleared whenever the next stop cannot be
/// the direct consequence of a step (start of Dbg, continue, link error).
class ExecutionHistory
{
public:
    static const int MAX_ENTRIES = 2;
    static const int MAX_ROWS = MAX_ENTRIES;

    void Clear() { mnCount = 0; }
    /// @brief Record an executed instruction (the most recent one). The oldest is dropped when full.
    /// @param sp Effective a7 when it was about to run
    /// @param forceBreak true to display it as a "from" line even if the next PC is contiguous (T8: step over)
    void Push(const DisassembledInstruction &instruction, uint32_t sp, bool forceBreak = false);
    int GetCount() const { return mnCount; }
    /// @brief Entry by age: 0 = oldest, GetCount() - 1 = most recent (the instruction executed just before the current stop)
    const ExecutedInstruction &Get(int index) const { return maEntries[index]; }
    /// @brief The most recent entry (previous PC, previous SP, size), nullptr if empty
    const ExecutedInstruction *GetPrevious() const { return mnCount > 0 ? &maEntries[mnCount - 1] : nullptr; }
    /// @brief Lines to display above the instruction at currentPc (oldest first):
    /// - most recent entry contiguous with currentPc: it is shown as a normal line, preceded by the older entry if that
    ///   one is contiguous with it too (an older entry that is not contiguous is dropped, no marker);
    /// - flow break (or unreliable size, or forceBreak): only the most recent entry, as a "from" line.
    /// @param rows Array of MAX_ROWS rows
    /// @return Number of rows written
    int BuildRows(uint32_t currentPc, HistoryRow *rows) const;

private:
    ExecutedInstruction maEntries[MAX_ENTRIES];
    int mnCount = 0;
};

/// @brief Full screen (TUI) display of the 68000 debugger
class DebugTuiView
{
public:
    enum class State
    {
        Waiting,    // Waiting for a debug frame (after reset)
        Stopped,    // Frame displayed, waiting for a user command
        Running,    // Continue command sent
    };

    void Enter();
    void Leave();
    void Redraw(bool querySize = true);
    void ShowFrame(const DebugFrame *frame, const DebugFrame *previousFrame, uint32_t stepCount);
    void SetState(State state);
    /// @brief Target of the running RUN_UNTIL, shown in the title bar while Running ("Running (until $xxxxxx)").
    /// Call before SetState(Running). hasTarget false: plain "Running" (CONTINUE).
    void SetRunTarget(bool hasTarget, uint32_t target) { mbRunTargetValid = hasTarget; mnRunTarget = target; }
    /// @brief History displayed above the current instruction in the Program panel (nullptr: none). Must stay valid;
    /// set before ShowFrame().
    void SetHistory(const ExecutionHistory *history) { mpHistory = history; }
    void SetMessage(const char *message, bool isError = false);
    void PollAsyncMessages();
    /// @brief Prompt in the message line and read a line (see Tui::ReadLine). Message line cleared afterwards.
    bool PromptInput(const char *prompt, char *buffer, size_t size, void (*idle)());

    /// @brief Number of memory lines (16 bytes each) to read for the memory panel: 0 if the panel is not displayed.
    /// Only meaningful once the view is entered (the layout depends on the terminal size).
    int GetMemoryLines() const { return mbShowMemory ? mnMemoryLines : 0; }
    /// @brief true if both panels are displayed together (tall terminal), so F3 has no effect
    bool IsBothPanelsLayout() const { return mbBothPanels; }
    bool IsMemoryPanelVisible() const { return mbShowMemory; }
    /// @brief Switch between the code and the memory panel (small terminals only, see ComputeLayout()). Redraws everything.
    /// @return true if the toggle was applied (false when both panels are already displayed)
    bool ToggleMemoryPanel();
    /// @brief Update the memory panel. The dump must stay valid as long as it is displayed.
    /// @param dump Source of the bytes (and of the changed bytes), may be nullptr
    /// @param hasAddress false: no address chosen yet, a hint is displayed
    /// @param address First address displayed (a multiple of 16)
    /// @param error nullptr, or a text displayed instead of the dump (read refused / failed)
    /// @brief Enable / disable the memory monitor (layout and screen updated when the state changes)
    void SetMemoryEnabled(bool enabled);
    /// @brief true: 16-bit words (default), false: bytes
    void SetMemoryWordMode(bool words);
    void ShowMemory(const MemoryDump *dump, bool hasAddress, uint32_t address, const char *error);

private:
    void ComputeLayout();
    void DrawAll();
    void DrawStatic();
    void DrawTitle();
    void DrawRegisters();
    void DrawStatus();
    void DrawCode();
    void DrawMemory();
    void DrawMessage();
    void DrawHelp();
    void DrawTooSmall();

    const DebugFrame *mpFrame = nullptr;
    const DebugFrame *mpPreviousFrame = nullptr;
    const ExecutionHistory *mpHistory = nullptr;
    uint32_t mnStepCount = 0;
    State meState = State::Waiting;
    bool mbRunTargetValid = false;
    uint32_t mnRunTarget = 0;
    char mszMessage[160] = "";
    bool mbMessageIsError = false;

    // Memory panel
    bool mbMemoryWords = true;
    bool mbMemorySelected = false;      // Small terminal: memory panel displayed instead of the code panel
    const MemoryDump *mpMemory = nullptr;
    bool mbHasMemoryAddress = false;
    uint32_t mnMemoryAddress = 0;
    char mszMemoryError[96] = "";

    // Layout, computed from the terminal size by ComputeLayout()
    uint16_t mnScreenWidth = 80;
    uint16_t mnScreenHeight = 24;
    uint16_t mnWidthRegisters = 39;
    uint16_t mnColAddressRegisters = 21;
    uint16_t mnColStatus = 40;
    uint16_t mnWidthStatus = 41;
    uint16_t mnRowCode = 13;
    uint16_t mnHeightCode = 10;
    int mnCodeLines = 8;
    uint16_t mnRowMemory = 13;
    uint16_t mnHeightMemory = 0;
    int mnMemoryLines = 0;
    bool mbBothPanels = false;
    bool mbShowCode = true;
    bool mbShowMemory = false;
    uint16_t mnRowMessage = 23;
    uint16_t mnRowHelp = 24;
};
