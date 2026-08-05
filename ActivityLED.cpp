#include "ActivityLED.h"
#include <pico/stdlib.h>

#define TIMER_DELAY_MILLISECONDS 50

ActivityLED::ActivityType ActivityLED::geCurrentActivityType = ActivityLED::ActivityType::DataTransfer;
uint ActivityLED::gnTickCount = 0xffffffff;
repeating_timer ActivityLED::gsTimerConfig;

void ActivityLED::Setup()
{
    // Setup LED pins
    gpio_init(PIN_ACTIVITY_LED);
    gpio_set_dir(PIN_ACTIVITY_LED, GPIO_OUT);

    // Setup the repeating timer for LED animation
    add_repeating_timer_ms(-1 * TIMER_DELAY_MILLISECONDS, IrqHandler, NULL, &gsTimerConfig);
}

void ActivityLED::SetActivityType(ActivityLED::ActivityType eType)
{
    geCurrentActivityType = eType;
}

void ActivityLED::ForceState(bool bOn)
{
    gpio_put(PIN_ACTIVITY_LED, bOn? 1 : 0);
}

bool ActivityLED::IrqHandler(struct repeating_timer *t)
{
    // Handle the LED
    switch (geCurrentActivityType)
    {
    case ActivityType::Off:
        gpio_put(PIN_ACTIVITY_LED, 0);
        break;
    case ActivityType::Idle:
        if (gnTickCount == 0)
            gpio_put(PIN_ACTIVITY_LED, 1);
        else
            gpio_put(PIN_ACTIVITY_LED, 0);
        break;
    case ActivityType::TerminalConnected:
        gpio_put(PIN_ACTIVITY_LED, 1);
        break;
    case ActivityType::RemoteKeyboardConnected:
        if ((gnTickCount >> 4) & 0x1)
            gpio_put(PIN_ACTIVITY_LED, 1);
        else
            gpio_put(PIN_ACTIVITY_LED, 0);
        break;
    case ActivityType::DataTransfer:
        if ((gnTickCount >> 0) & 0x1)
            gpio_put(PIN_ACTIVITY_LED, 1);
        else
            gpio_put(PIN_ACTIVITY_LED, 0);
        break;
    default:
        // Should not reach here
        gpio_put(PIN_ACTIVITY_LED, 0);
        break;
    }

    // Handle rollover
    if (++gnTickCount >= 32)
    {
        gnTickCount = 0;
    }

    return true;
}
