#pragma once

#include <pico/time.h>
#include "PinDefinitions.h"

class ActivityLED
{
public:
    enum class ActivityType
    {
        Off,
        Idle,
        TerminalConnected,
        RemoteKeyboardConnected,
        DataTransfer
    };
    static void Setup();
    static void SetActivityType(ActivityType eType);
    static void ForceState(bool bOn);
private:
    static bool IrqHandler(struct repeating_timer *t);
    static ActivityType geCurrentActivityType;
    static uint gnTickCount;
    static repeating_timer gsTimerConfig;
};