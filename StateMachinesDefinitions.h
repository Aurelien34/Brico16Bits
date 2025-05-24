#pragma once

#include "PioBusAccess.pio.h"
#include "PioBusWatcherPort.pio.h"
#include "PioBusWatcherAddresses.pio.h"
#include "PioFrequencyCounter.pio.h"

#define PIO_DATA_INPUT_EOT_IRQ (BusWatcherPort_irq_number_notify_core)

#if (BusWatcherPort_wrap + 1 + BusReader_wrap + 1 == 28)
#error "State machines programs don't fit in PIO0"
#endif

#if (BusWatcherAddresses_wrap + 1 + BusReader_wrap + 1 == 28)
#error "State machines programs don't fit in PIO0"
#endif

#if (BusWatcherPort_irq_number_sm_sync  != BusAccess_irq_number_sm_sync)
#error "IRQ mismatch in state machines definitions"
#endif

#if (BusWatcherAddresses_irq_number_sm_sync  != BusAccess_irq_number_sm_sync)
#error "IRQ mismatch in state machines definitions"
#endif