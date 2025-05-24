#pragma once
#include <hardware/pio.h>

#define PIO_BUS_READER pio0
#define STATE_MACHINE_BUS_READER 0
#define STATE_MACHINE_BUS_WATCHER 1
#define PIO_FREQUENCY_COUNTER pio1
#define STATE_MACHINE_FREQUENCY_COUNTER 0

class PIOManager
{
public:
    enum class BusWatcherType
    {
        Port,
        Addresses
    };
    static void InitBusPIO();
    static void InitBusReaderStateMachine();
    static void InitBusWatcherStateMachine(BusWatcherType eWatcherType);
    // Push the base ROM image address and start listening to the bus
    static void PushImageAddress(void *pImageAddress);
    static inline BusWatcherType GetBusWatcherType() { return geBusWatcherType; }
    static void InitFrequencyCounterPIO();
    static void InitFrequencyCounterReaderStateMachine();
    static uint32_t ComputeFrequency();
    static void ForceWatchdogReset();
private:
    static const pio_program *gpInstalledWatherProgram;
    static uint guiBusWatcherProgramOffset;
    static uint32_t guPortAddress;
    static BusWatcherType geBusWatcherType;
};