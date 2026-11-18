#pragma once

#include <cstdint>

#define DEBUG_FRAME_CODE_WORDS 25
#define DEBUG_INSTRUCTION_MAX_WORDS 5   // Longest 68000 instruction: opcode + 4 extension words
#define DEBUG_INSTRUCTION_MAX_TEXT 100

/// @brief Decoded content of a debug frame sent by the 68k trace routine
struct DebugFrame
{
    uint32_t d[8];
    uint32_t a[8];      // a[7] is the effective a7 (SR.S ? ssp : usp)
    uint32_t ssp;       // Supervisor stack pointer before the exception
    uint32_t usp;       // User stack pointer
    uint16_t sr;
    uint32_t pc;
    uint16_t code[DEBUG_FRAME_CODE_WORDS];  // Memory starting at PC
    uint16_t frameNumber;
};

/// @brief One disassembled instruction of a debug frame, ready to be displayed by any view
struct DisassembledInstruction
{
    uint32_t address;
    char text[DEBUG_INSTRUCTION_MAX_TEXT];      // "Unknown" if the opcode could not be decoded
    uint16_t words[DEBUG_INSTRUCTION_MAX_WORDS];
    uint8_t wordCount;
    bool truncated;     // The instruction extends past the code sent by the 68k: text and words are incomplete
};

int DisassembleFrame(const DebugFrame *frame, DisassembledInstruction *instructions, int maxInstructions);
