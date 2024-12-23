#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "NeoDiagImageBuilder.h"
#include "BricoExtManager.h"
#include "MemoryMap.h"

#define INPUT_SIZE        0x8000	// how large the resulting file from vasm should be
#define MIRROR_OFFSET     INPUT_SIZE - 5
#define CRC32_OFFSET      INPUT_SIZE - 4
#define NUM_MIRRORS       3


uint32_t NeoDiagImageBuilder::crc32_for_byte(uint32_t r)
{
  for(int j = 0; j < 8; ++j)
    r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
  return r ^ (uint32_t)0xFF000000L;
}

void NeoDiagImageBuilder::crc32(const void *data, size_t n_bytes, uint32_t* crc)
{
  static uint32_t table[0x100];
  if(!*table)
    for(size_t i = 0; i < 0x100; ++i)
      table[i] = crc32_for_byte(i);
  for(size_t i = 0; i < n_bytes; ++i)
    *crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[(i % 2 == 0) ? (i + 1) : (i - 1)]] ^ *crc >> 8;
}

bool NeoDiagImageBuilder::CheckAndBuildImage()
{
    bool bNeoDiagIdentified = false;
    if (BricoExtManager::IsBricoNeoROM() && ((int)BricoExtManager::GetSpecialModeFlags() & (int)BricoExtManager::SpecialModeFlags::NeoDiagCRCMirrorComputation))
    {
        bNeoDiagIdentified = true;

        char * buffer = (char *)BASE_ADDRESS_IMAGE_IN_RAM;
        int i;
        uint32_t crc = 0;

        crc32(buffer, INPUT_SIZE - 5, &crc);
#ifdef MASS_STORAGE_DEBUG
        printf("Fill in CRC: 0x%x\n", crc);
#endif
        // Fix int32 endianness
        crc = (((crc >> 0) & 0xff) << 24) + (((crc >> 8) & 0xff) << 16) + (((crc >> 16) & 0xff) << 8) +  + (((crc >> 24) & 0xff) << 0);

        // write crc
        *(uint32_t *)(buffer + CRC32_OFFSET) = crc;

        char *rom = buffer;
        for(i = 1; i <= NUM_MIRRORS;i++)
        {
            rom += INPUT_SIZE;
            memcpy(rom, buffer, INPUT_SIZE - 5);

            // zero out the crc location, mirrors dont get it filled in
            *(uint32_t *)(rom + CRC32_OFFSET) = 0;
            rom[MIRROR_OFFSET] = i;
        }
    }

   return bNeoDiagIdentified;
}