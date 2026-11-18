#pragma once

#include <cstdint>

#include "DebugFrame.h"
#include "MemoryDump.h"

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
    uint32_t mnStepCount = 0;
    State meState = State::Waiting;
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
