#pragma once

#include <hardware/dma.h>

#define DMA_CHANNEL_WRITE_DATA                  0
#define DMA_CHANNEL_READ_ADDRESSES              1
#define DMA_CHANNEL_SYNCHRONOUS_MEMORY_COPY     2
#define DMA_CHANNEL_INPUT_BUFFER                3
#define DMA_CHANNEL_INPUT_BUFFER_TRIGGER        4

class DMAManager
{
public:
    static void InitChannelsBusReader(void *pImageBaseAddress);
    static void InitChannelAsyncMemoryCopy();
    static void InitChannelBusWatcher(void *pBufferAddress, bool reintit = false);
    static void DMAStartCopyDW(void *pSource, void *pTarget, size_t dwordsToCopy);
    static void DMAEndCopyDW();
private:
    static dma_channel_config oConfigSync;
};
