#pragma once

#include <cstdint>

#include "DebugFrame.h"

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

private:
    void ComputeLayout();
    void DrawAll();
    void DrawStatic();
    void DrawTitle();
    void DrawRegisters();
    void DrawStatus();
    void DrawCode();
    void DrawMessage();
    void DrawHelp();
    void DrawTooSmall();

    const DebugFrame *mpFrame = nullptr;
    const DebugFrame *mpPreviousFrame = nullptr;
    uint32_t mnStepCount = 0;
    State meState = State::Waiting;
    char mszMessage[160] = "";
    bool mbMessageIsError = false;

    // Layout, computed from the terminal size by ComputeLayout()
    uint16_t mnScreenWidth = 80;
    uint16_t mnScreenHeight = 24;
    uint16_t mnWidthRegisters = 39;
    uint16_t mnColAddressRegisters = 21;
    uint16_t mnColStatus = 40;
    uint16_t mnWidthStatus = 41;
    uint16_t mnHeightCode = 10;
    int mnCodeLines = 8;
    uint16_t mnRowMessage = 23;
    uint16_t mnRowHelp = 24;
};
