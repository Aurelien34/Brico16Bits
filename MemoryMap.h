#pragma once


/*

New memory map (from memmap_with_rom_image.ld)

*/

// RP2040
// Base addresses are defined and reserved in the memmap linker file
#define BASE_ADDRESS_IMAGE_IN_RAM           0x20020000
#define BASE_ADDRESS_INPUT_RING_BUFFER      0x2001F000

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
