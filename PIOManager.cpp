#include <stdio.h>
#include <pico/stdlib.h>

#include "PinDefinitions.h"
#include "PIOManager.h"
#include "ActivityLED.h"

#define WATCHDOG_RESET_TIME_MILLISECONDS 500

#define DEFAULT_OUTPUT_PORT_ADDRESS 0x1FF02

uint PIOManager::guiBusWatcherProgramOffset = 0xffffffff;
const pio_program *PIOManager::gpInstalledWatherProgram = NULL;
uint32_t PIOManager::guPortAddress = DEFAULT_OUTPUT_PORT_ADDRESS;
PIOManager::BusWatcherType PIOManager::geBusWatcherType;

void PIOManager::InitBusPIO()
{
    // Set pin's GPIO function (connect PIO to the pad)
    for (int pin = PIN_BUS_ORIGIN; pin < PIN_BUS_ORIGIN + BUS_BIT_COUNT_ADDRESS; ++pin )
    {
        pio_gpio_init(PIO_BUS_READER, pin);
    }

    // Assign additional bus control pins
    pio_gpio_init(PIO_BUS_READER, PIN_ADDRESS_INPUT_ENABLE);
    pio_gpio_init(PIO_BUS_READER, PIN_DATA_OUTPUT_ENABLE);
    pio_gpio_init(PIO_BUS_READER, PIN_ROM_ACCESS);
}

void PIOManager::InitBusReaderStateMachine()
{
    // Stop the state machine
    pio_sm_set_enabled(PIO_BUS_READER, STATE_MACHINE_BUS_READER, false);

    // State machine 0: Bus Reader => Set pins function
    uint programOffset = pio_add_program(PIO_BUS_READER, &BusAccess_program);
    pio_sm_config config = BusAccess_program_get_default_config(programOffset);
    sm_config_set_in_pins(&config, PIN_BUS_ORIGIN);
    sm_config_set_out_pins(&config, PIN_BUS_ORIGIN + 1, BUS_BIT_COUNT_DATA); // First address bit is forced to 0 to always read even address. Data start 1 pin further
    sm_config_set_sideset_pins(&config, PIN_SIDESET_START);
    sm_config_set_in_shift(&config, false, false, 0); // Set read order to reverse so that we can preload the base address in the PIO

    // Load our configuration, and jump to the start of the program
    pio_sm_init(PIO_BUS_READER, STATE_MACHINE_BUS_READER, programOffset, &config);

    // Force pin directions
    pio_sm_set_consecutive_pindirs(PIO_BUS_READER, STATE_MACHINE_BUS_READER, PIN_BUS_ORIGIN, BUS_BIT_COUNT_ADDRESS, false); // Input
    pio_sm_set_consecutive_pindirs(PIO_BUS_READER, STATE_MACHINE_BUS_READER, PIN_ADDRESS_INPUT_ENABLE, 1, true);    // Output
    pio_sm_set_consecutive_pindirs(PIO_BUS_READER, STATE_MACHINE_BUS_READER, PIN_DATA_OUTPUT_ENABLE, 1, true);// Output
    pio_sm_set_consecutive_pindirs(PIO_BUS_READER, STATE_MACHINE_BUS_READER, PIN_ROM_ACCESS, 1, false); // Input

    // Start the state machine
    pio_sm_set_enabled(PIO_BUS_READER, STATE_MACHINE_BUS_READER, true);
}

void PIOManager::PushImageAddress(void *pImageAddress)
{
    // Send the base image address
    pio_sm_put(PIO_BUS_READER, STATE_MACHINE_BUS_READER, ((uint32_t)pImageAddress) >> BusAccess_bus_size_address);
}

void PIOManager::InitBusWatcherStateMachine(PIOManager::BusWatcherType eWatcherType)
{
    // Store state machine type for future configuration
    geBusWatcherType = eWatcherType;

    // Stop the state machine
    pio_sm_set_enabled(PIO_BUS_READER, STATE_MACHINE_BUS_WATCHER, false);

    // Select the expected program
    const pio_program *newProgram = eWatcherType == BusWatcherType::Port ? &BusWatcherPort_program : &BusWatcherAddresses_program;

    // Need to remove previous program?
    if (guiBusWatcherProgramOffset != 0xffffffff)
    {
        // Remove previous program
        pio_remove_program(PIO_BUS_READER, gpInstalledWatherProgram, guiBusWatcherProgramOffset);
    }
    
    // Install the new one instead
    guiBusWatcherProgramOffset = pio_add_program(PIO_BUS_READER, newProgram);
    gpInstalledWatherProgram = newProgram;

    // State machine 0: Bus Reader => Set pins function
    pio_sm_config config =
        eWatcherType == BusWatcherType::Port ?
            BusWatcherPort_program_get_default_config(guiBusWatcherProgramOffset)
            : BusWatcherAddresses_program_get_default_config(guiBusWatcherProgramOffset);
    sm_config_set_in_pins(&config, PIN_BUS_ORIGIN + 1); // Ignore the lower address bit, as we only read 16 bits words addresses
    sm_config_set_in_shift(&config, false, false, 0); // Set read order to reverse so that we can preload the base address in the PIO

    // Load our configuration, and jump to the start of the program
    pio_sm_init(PIO_BUS_READER, STATE_MACHINE_BUS_WATCHER, guiBusWatcherProgramOffset, &config);

    // Start the state machine
    pio_sm_set_enabled(PIO_BUS_READER, STATE_MACHINE_BUS_WATCHER, true);

    // Push port address if required
    if (eWatcherType == BusWatcherType::Port)
    {
        pio_sm_put(PIO_BUS_READER, STATE_MACHINE_BUS_WATCHER, guPortAddress >> 1);
    }
}

void PIOManager::ForceWatchdogReset()
{
    // Stop the state machine
    pio_sm_set_enabled(PIO_BUS_READER, STATE_MACHINE_BUS_READER, false);

    // Wait for watchdog do trigger
    sleep_ms(WATCHDOG_RESET_TIME_MILLISECONDS);

    // Start the state machine
    pio_sm_set_enabled(PIO_BUS_READER, STATE_MACHINE_BUS_READER, true);
}


void PIOManager::InitFrequencyCounterPIO()
{
    // Set pin's GPIO function (connect PIO to the pad)
    gpio_init(PIN_EXTENDED_IN);
    gpio_set_dir(PIN_EXTENDED_IN, GPIO_IN);
    pio_gpio_init(PIO_FREQUENCY_COUNTER, PIN_EXTENDED_IN);
}

void PIOManager::InitFrequencyCounterReaderStateMachine()
{
    // Stop the state machine
    pio_sm_set_enabled(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, false);

    // Set pins function
    uint programOffset = pio_add_program(PIO_FREQUENCY_COUNTER, &FrequencyCounter_program);
    pio_sm_config config = FrequencyCounter_program_get_default_config(programOffset);
    sm_config_set_jmp_pin(&config, PIN_EXTENDED_IN);

    // Load our configuration, and jump to the start of the program
    pio_sm_init(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, programOffset, &config);

    // Force pin directions
    pio_sm_set_consecutive_pindirs(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, PIN_EXTENDED_IN, 1, false); // Input
}

uint32_t PIOManager::ComputeFrequency()
{
    // Start the state machine
    pio_sm_set_enabled(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, true);

    uint32_t sampleCountInitial = 10000000;
    pio_sm_put_blocking(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, sampleCountInitial);
    uint32_t periodsCount = pio_sm_get_blocking(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER) + 1;

    // Stop the state machine
    pio_sm_set_enabled(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, true);

    int sampleCount = sampleCountInitial + periodsCount;
    int frequency = (uint32_t)(((double)periodsCount * (double)sampleCountInitial) / (double)sampleCount * 10.0 + 0.5);
    return frequency < 100 ? 0: frequency;
    /*
    // Start the state machine
    pio_sm_set_enabled(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, true);

    uint32_t sampleCountInitial = 100000000;
    pio_sm_put_blocking(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, sampleCountInitial);
    uint32_t periodsCount = pio_sm_get_blocking(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER) + 1;

    // Stop the state machine
    pio_sm_set_enabled(PIO_FREQUENCY_COUNTER, STATE_MACHINE_FREQUENCY_COUNTER, true);

    int sampleCount = sampleCountInitial + periodsCount;
    int frequency = (uint32_t)(((double)periodsCount * (double)sampleCountInitial) / (double)sampleCount + 0.5);
    return frequency <= 1 ? 0: frequency;
    */
}
