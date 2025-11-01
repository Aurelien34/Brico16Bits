#pragma once


/*

New memory map (from memmap_with_rom_image.ld)

*/

// Microcontroller
// Base addresses are defined and reserved in the memmap linker file
#ifdef PICO_RP2040
#define BASE_ADDRESS_IMAGE_IN_RAM           0x20020000
#endif
#ifdef PICO_RP2350
#define BASE_ADDRESS_IMAGE_IN_RAM           0x20060000
#endif

#define BASE_ADDRESS_INPUT_RING_BUFFER      ((BASE_ADDRESS_IMAGE_IN_RAM)-4096)//0x2001F000

// NeoGeo
// Fix addresses in ROM
#define ROM_OFFSET_MAGIC_WORD               0x00000080
#define ROM_OFFSET_VERSION                  ROM_OFFSET_MAGIC_WORD + 8
#define ROM_OFFSET_SPECIAL_MODE_FLAGS       ROM_OFFSET_VERSION + 4
#define ROM_OFFSET_JMP_OPCODE               ROM_OFFSET_SPECIAL_MODE_FLAGS + 2
#define ROM_OFFSET_COMMAND_ADDRESS          ROM_OFFSET_JMP_OPCODE + 2
#define ROM_OFFSET_COMMAND_PARAMETER_1      ROM_OFFSET_COMMAND_ADDRESS + 4
#define ROM_OFFSET_COMMAND_PARAMETER_2      ROM_OFFSET_COMMAND_PARAMETER_1 + 4
#define ROM_OFFSET_COMMAND_PARAMETER_3      ROM_OFFSET_COMMAND_PARAMETER_2 + 4
#define ROM_OFFSET_EXTENSION_TABLE          ROM_OFFSET_COMMAND_PARAMETER_3 + 4

#define ROM_OFFSET_OFFICIAL_TRACE_VECTOR    0x000024
#define ROM_OFFSET_OFFICIAL_RANDOM_NUMBERS  0x004200