#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <tusb.h>
#include <pico/multicore.h>
#include <hardware/structs/pio.h>
#include <hardware/clocks.h>

#include "PinDefinitions.h"
#include "DMAManager.h"
#include "PIOManager.h"
#include "ROMImageManager.h"
#include "CommandManager.h"
#include "ActivityLED.h"
#include "InputBuffer.h"
#include "MassStorageManager.h"
#include "CommandManager68k.h"

// See MemoryMap.h for memory mapping

int main()
{
    // Overclock the RP2040 to 200MHz or 250MHz (change stabilization delay constant "delay_address_stabilization" in PioBusAccess.pio to set clock value)
    set_sys_clock_khz(200000, true);    // 200 MHz

    //multicore_reset_core1();

    // Init DMA channel now in order to be able to perform copy image from flash to RAM during intialization
    DMAManager::InitChannelAsyncMemoryCopy();

    // Start on the ROM image in bank 0 by default (using DMA initialized above)
    ROMImageManager::GetFromFlashAsyncStart(FLASH_ROM_BANK_DEFAULT);

    // Init EPROM mode state machines
    PIOManager::InitBusPIO();
    PIOManager::InitBusReaderStateMachine();
    PIOManager::InitBusWatcherStateMachine(PIOManager::BusWatcherType::Port);

    // Init bus reader DMA channels
    DMAManager::InitChannelsBusReader(ROMImageManager::GetImageBaseAddressDW());

    // Wait for ROM to RAM transfer to complete
    ROMImageManager::GetFromFlashAsyncEnd();

    // Push the base ROM image address to the state machine to start listening to the bus
    PIOManager::PushImageAddress(ROMImageManager::GetImageBaseAddressDW());

    // From here, the board is behaving as an EPROM!!!

    // Init frequency counter state machine
    PIOManager::InitFrequencyCounterPIO();
    PIOManager::InitFrequencyCounterReaderStateMachine();

    // Init input port DMA channels
    DMAManager::InitChannelBusWatcher(InputBuffer::GetBufferAddress());

    // Init status LED
    ActivityLED::Setup();

    // Init mass storage
    MassStorageManager::Setup();

    // Init STDIO
    stdio_init_all();

    // Init USB connection
    CommandManager::InitSerial();

    while(true)
    {
        // Handle user inputs
        CommandManager::HandlePrompt();
        MassStorageManager::Loop();
        CommandManager68k::HandleCommands();
    }
}

