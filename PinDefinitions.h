#pragma once

#include "StateMachinesDefinitions.h"

// Bus lines
#define BUS_BIT_COUNT_TOTAL         BusAccess_bus_size_address
#define BUS_BIT_COUNT_DATA          BusAccess_bus_size_data
#define BUS_BIT_COUNT_ADDRESS       BusAccess_bus_size_address
#define PIN_BUS_ORIGIN              0

// Bus management
#define PIN_ADDRESS_INPUT_ENABLE    17
#define PIN_DATA_OUTPUT_ENABLE      18
#define PIN_ROM_ACCESS              22

#if (PIN_ROM_ACCESS != BusAccess_gpio_access)
#error Check ROM access pin number (declaration different from PIO code)
#endif

// PIO specification
#define PIN_SIDESET_START           PIN_ADDRESS_INPUT_ENABLE

// Extended options (for later use)
#define PIN_EXTENDED_OUT            19
#define PIN_EXTENDED_IN             26
#define PIN_SLOT_POWER              27 // Wired from v0.6 of the board

// Activity LED
#define PIN_ACTIVITY_LED            28
