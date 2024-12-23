#include "InputBuffer.h"
#include "DMAManager.h"
#include "StateMachinesDefinitions.h"

#define ADDRESS_START (uint16_t *)(BASE_ADDRESS_INPUT_RING_BUFFER)
#define ADDRESS_END ((uint16_t *)(BASE_ADDRESS_INPUT_RING_BUFFER + (1 << INPUT_BUFFER_SIZE_POWEROF2)))

uint16_t *InputBuffer::gpNextReadPosition = ADDRESS_START;
uint32_t InputBuffer::lastBufferCRC;

bool InputBuffer::IsEmpty()
{
    return (gpNextReadPosition == (uint16_t *)dma_channel_hw_addr(DMA_CHANNEL_INPUT_BUFFER)->write_addr);
}

uint16_t InputBuffer::GetValue()
{
    uint16_t value;
    while (!GetValue(&value)) { }
    return value;
}

bool InputBuffer::GetValue(uint16_t *value)
{
    if (!IsEmpty())
    {
        *value = *(uint16_t *)(gpNextReadPosition++);
        if (gpNextReadPosition >= ADDRESS_END)
        {
            gpNextReadPosition = ADDRESS_START;
        }
        return true;
    }
    else
    {
        return false;
    }
}

void InputBuffer::Clear(bool clearEOT)
{
    // Point to the next address to be read by the DMA channel
    gpNextReadPosition = (uint16_t *)(void *)(dma_channel_hw_addr(DMA_CHANNEL_INPUT_BUFFER)->write_addr);
    
    // Clear EOT signal
    if (clearEOT)
    {
        CheckEOTSignalAndClearEOT();
    }
}

bool InputBuffer::CheckEOTSignalAndClearEOT()
{
    bool bEOT = pio0_hw->irq & (1 << PIO_DATA_INPUT_EOT_IRQ);
    if (bEOT)
    {
        pio0_hw->irq = (1 << PIO_DATA_INPUT_EOT_IRQ);
        
        // Get DMA CRC
        lastBufferCRC = dma_sniffer_get_data_accumulator();

        // Reset DMA CRC calculation
        dma_sniffer_set_data_accumulator(0xffffffff);
    }
    return bEOT;
}
