#pragma once

/// @brief Control flow category of a 68000 instruction, as seen by the debugger.
/// Kept in its own header (no dependencies) so that the instruction classifier and the call
/// stack analyzer can share it without including each other.
enum class InstructionFlow
{
    Other,   ///< Any other instruction (including BRA, Bcc, JMP)
    Call,    ///< JSR, BSR : pushes a return address (4 bytes)
    Trap,    ///< TRAP #n, TRAPV : exception frame SR + PC (6 bytes) when taken
    Loop,    ///< DBcc
    Return   ///< RTS, RTR, RTE
};
