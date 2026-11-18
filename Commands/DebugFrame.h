#pragma once

#include <cstdint>

#include "InstructionFlow.h"

#define DEBUG_FRAME_CODE_WORDS 50      // Capacity: code words read at PC at each stop (one READ_MEM)
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
    uint16_t codeWords;                     // Number of valid words in code[] (fewer if the read was refused)
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
    bool hasTarget;     // Relative branch / jump: target holds the absolute destination address (24 bits)
    uint32_t target;
};

/// @brief Absolute destination of a relative branch, from the instruction words: Bcc / BRA / BSR (8 or 16-bit
/// displacement), DBcc, and JMP / JSR with d16(PC) addressing. Other instructions (and other addressing modes) have no
/// target. The result is masked on 24 bits.
/// @param address Address of the instruction (first word)
/// @param words Instruction words, wordCount >= 1
/// @return false if the instruction has no computable target
inline bool GetBranchTarget(uint32_t address, const uint16_t *words, int wordCount, uint32_t *target)
{
    if (wordCount < 1)
    {
        return false;
    }
    uint16_t op = words[0];
    int32_t displacement;
    if ((op & 0xf000) == 0x6000)
    {
        uint8_t low = (uint8_t)(op & 0xff);
        if (low == 0xff)
        {
            return false;   // 32-bit displacement: not valid on 68000
        }
        if (low == 0)
        {
            if (wordCount < 2)
            {
                return false;
            }
            displacement = (int16_t)words[1];
        }
        else
        {
            displacement = (int8_t)low;
        }
    }
    else if ((op & 0xf0f8) == 0x50c8 || (((op & 0xffc0) == 0x4e80 || (op & 0xffc0) == 0x4ec0) && (op & 0x3f) == 0x3a))
    {
        // DBcc, or JSR / JMP d16(PC) (mode 7, register 2)
        if (wordCount < 2)
        {
            return false;
        }
        displacement = (int16_t)words[1];
    }
    else
    {
        return false;
    }
    *target = (uint32_t)((int32_t)address + 2 + displacement) & 0xffffff;
    return true;
}

/// @brief Control flow category of an instruction, from its first word (extension words are not needed):
/// Call (JSR with a control addressing mode, BSR 8 / 16-bit), Trap (TRAP #n, TRAPV), Loop (DBcc),
/// Return (RTS, RTR, RTE), Other (everything else, including BRA, Bcc, JMP, STOP, RESET).
/// Pure function (no decoder needed), tested on the host.
/// @param words Instruction words, at least one
inline InstructionFlow ClassifyInstruction(const uint16_t *words)
{
    uint16_t op = words[0];
    if ((op & 0xffc0) == 0x4e80)
    {
        // JSR <ea>: only control addressing modes (An), d16(An), d8(An,Xn), abs.W, abs.L, d16(PC), d8(PC,Xn)
        unsigned mode = (op >> 3) & 7;
        unsigned reg = op & 7;
        bool valid = mode == 2 || mode == 5 || mode == 6 || (mode == 7 && reg <= 3);
        return valid ? InstructionFlow::Call : InstructionFlow::Other;
    }
    if ((op & 0xff00) == 0x6100)
    {
        // BSR.S (8-bit displacement), BSR.W ($6100 + word); $61FF (32-bit displacement) does not exist on 68000
        return (op & 0xff) == 0xff ? InstructionFlow::Other : InstructionFlow::Call;
    }
    if ((op & 0xfff0) == 0x4e40 || op == 0x4e76)
    {
        return InstructionFlow::Trap;       // TRAP #n, TRAPV
    }
    if ((op & 0xf0f8) == 0x50c8)
    {
        return InstructionFlow::Loop;       // DBcc Dn,<disp>
    }
    if (op == 0x4e75 || op == 0x4e77 || op == 0x4e73)
    {
        return InstructionFlow::Return;     // RTS, RTR, RTE
    }
    return InstructionFlow::Other;
}

int DisassembleFrame(const DebugFrame *frame, DisassembledInstruction *instructions, int maxInstructions);
