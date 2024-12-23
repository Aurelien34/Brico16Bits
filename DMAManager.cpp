#include <stdio.h>
#include <malloc.h>
#include <hardware/structs/bus_ctrl.h>
#include <pico/stdlib.h>

#include "DMAManager.h"
#include "PIOManager.h"
#include "InputBuffer.h"

dma_channel_config DMAManager::oConfigSync;

void DMAManager::InitChannelsBusReader(void *pImageBaseAddress)
{
    // Give the highest priority to DMA over CPUs
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // Configure data sending channel (triggered by DMA_CHANNEL_READ_ADDRESSES)
    dma_channel_claim(DMA_CHANNEL_WRITE_DATA);
    dma_channel_config dmaConfigSendData = dma_channel_get_default_config(DMA_CHANNEL_WRITE_DATA);
    channel_config_set_read_increment(&dmaConfigSendData, false);
    channel_config_set_write_increment(&dmaConfigSendData, false);
    channel_config_set_dreq(&dmaConfigSendData, DREQ_FORCE); // No regulation, transfer at startup
    channel_config_set_transfer_data_size(&dmaConfigSendData, DMA_SIZE_16);
    channel_config_set_high_priority(&dmaConfigSendData, true);
    channel_config_set_irq_quiet(&dmaConfigSendData, true);
    channel_config_set_chain_to(&dmaConfigSendData, DMA_CHANNEL_READ_ADDRESSES); // Retrigger DMA_CHANNEL_READ_ADDRESSES, paced by the DREQ signal
    dma_channel_configure(
        DMA_CHANNEL_WRITE_DATA,
        &dmaConfigSendData,
        &(PIO_BUS_READER->txf[STATE_MACHINE_BUS_READER]),
        0x00000000,  // Overwritten by DMA_CHANNEL_READ_ADDRESSES
        1,
        false);

    // Configure address reading channel
    dma_channel_claim(DMA_CHANNEL_READ_ADDRESSES);
    dma_channel_config dmaConfigReadAddress = dma_channel_get_default_config(DMA_CHANNEL_READ_ADDRESSES);
    channel_config_set_read_increment(&dmaConfigReadAddress, false);
    channel_config_set_write_increment(&dmaConfigReadAddress, false);
    channel_config_set_dreq(&dmaConfigReadAddress, pio_get_dreq(PIO_BUS_READER, STATE_MACHINE_BUS_READER, false));
    channel_config_set_transfer_data_size(&dmaConfigReadAddress, DMA_SIZE_32);
    channel_config_set_high_priority(&dmaConfigReadAddress, true);
    channel_config_set_irq_quiet(&dmaConfigReadAddress, true);
    dma_channel_configure(
        DMA_CHANNEL_READ_ADDRESSES,
        &dmaConfigReadAddress,
        &dma_channel_hw_addr(DMA_CHANNEL_WRITE_DATA)->al3_read_addr_trig,   // Sets address and triggers DMA_CHANNEL_WRITE_DATA
        &(PIO_BUS_READER->rxf[STATE_MACHINE_BUS_READER]),
        1, // Only one read, as the channel is retriggered by DMA_CHANNEL_WRITE_DATA after each write
        true);
    
}

void DMAManager::InitChannelAsyncMemoryCopy()
{
    // Configure DMA Channel for synchronous memory copy
    // Will be started on user request
    dma_channel_claim(DMA_CHANNEL_SYNCHRONOUS_MEMORY_COPY);
    oConfigSync = dma_channel_get_default_config(DMA_CHANNEL_SYNCHRONOUS_MEMORY_COPY);
    channel_config_set_read_increment(&oConfigSync, true);
    channel_config_set_write_increment(&oConfigSync, true);
    channel_config_set_dreq(&oConfigSync, DREQ_FORCE);
    channel_config_set_transfer_data_size(&oConfigSync, DMA_SIZE_32);
    channel_config_set_high_priority(&oConfigSync, true);
    channel_config_set_irq_quiet(&oConfigSync, true);
}

void DMAManager::InitChannelBusWatcher(void *pBufferAddress, bool reintit)
{
    // Configure input data reading buffer
    // Data come from PIO0/SM1 rx fifo and goes to a ring buffer in fixed aligned memory location
    // Channel DMA_CHANNEL_INPUT_BUFFER_TRIGGER triggers DMA_CHANNEL_INPUT_BUFFER_TRIGGER, that triggers it back
    if (!reintit)
    {
        dma_channel_claim(DMA_CHANNEL_INPUT_BUFFER_TRIGGER);
        dma_channel_config dmaConfigInputBufferTrigger = dma_channel_get_default_config(DMA_CHANNEL_INPUT_BUFFER_TRIGGER);
        channel_config_set_read_increment(&dmaConfigInputBufferTrigger, false);
        channel_config_set_write_increment(&dmaConfigInputBufferTrigger, false);
        channel_config_set_dreq(&dmaConfigInputBufferTrigger, DREQ_FORCE);
        channel_config_set_transfer_data_size(&dmaConfigInputBufferTrigger, DMA_SIZE_16);
        channel_config_set_high_priority(&dmaConfigInputBufferTrigger, false);
        channel_config_set_irq_quiet(&dmaConfigInputBufferTrigger, true);
        channel_config_set_chain_to(&dmaConfigInputBufferTrigger, DMA_CHANNEL_INPUT_BUFFER);
        dma_channel_configure(
            DMA_CHANNEL_INPUT_BUFFER_TRIGGER,
            &dmaConfigInputBufferTrigger,
            pBufferAddress,
            pBufferAddress,
            1,
            false);
    }

    if (!reintit)
    {
        dma_channel_claim(DMA_CHANNEL_INPUT_BUFFER);
    }
    dma_channel_config dmaConfigInputBuffer = dma_channel_get_default_config(DMA_CHANNEL_INPUT_BUFFER);
    channel_config_set_read_increment(&dmaConfigInputBuffer, false);
    channel_config_set_write_increment(&dmaConfigInputBuffer, true);
    channel_config_set_dreq(&dmaConfigInputBuffer, pio_get_dreq(PIO_BUS_READER, STATE_MACHINE_BUS_WATCHER, false));
    channel_config_set_transfer_data_size(&dmaConfigInputBuffer, DMA_SIZE_16);
    channel_config_set_high_priority(&dmaConfigInputBuffer, false);
    channel_config_set_irq_quiet(&dmaConfigInputBuffer, true);

    // Enable CRC32 calculation on this channel in order to compare buffer contents
    channel_config_set_sniff_enable(&dmaConfigInputBuffer, true);
    dma_sniffer_enable(DMA_CHANNEL_INPUT_BUFFER, DMA_SNIFF_CTRL_CALC_VALUE_CRC32, true);

    if (PIOManager::GetBusWatcherType() == PIOManager::BusWatcherType::Port)
    {
        // No ring in address mode
        channel_config_set_ring(&dmaConfigInputBuffer, true, INPUT_BUFFER_SIZE_POWEROF2);
        // Only trigger autoreload channel when reading data from port
        channel_config_set_chain_to(&dmaConfigInputBuffer, DMA_CHANNEL_INPUT_BUFFER_TRIGGER);
    }
    dma_channel_configure(
        DMA_CHANNEL_INPUT_BUFFER,
        &dmaConfigInputBuffer,
        pBufferAddress,
        &(PIO_BUS_READER->rxf[STATE_MACHINE_BUS_WATCHER]),
        (1 << (INPUT_BUFFER_SIZE_POWEROF2 - 1)) - 1, // Useless in port mode, as the channel is triggered by the other one after each transfer. But useful in address logging mode, where there is no ring buffer and no chain. The last "-1" is to avoir rollover => only 2047 values instead of 2048.
        true);
}

void DMAManager::DMAStartCopyDW(void *pSource, void *pTarget, size_t dwordsToCopy)
{
    dma_channel_configure(
        DMA_CHANNEL_SYNCHRONOUS_MEMORY_COPY,
        &oConfigSync,
        pTarget,
        pSource,
        dwordsToCopy >> 2,
        true);
}

void DMAManager::DMAEndCopyDW()
{
    dma_channel_wait_for_finish_blocking(DMA_CHANNEL_SYNCHRONOUS_MEMORY_COPY);
}
