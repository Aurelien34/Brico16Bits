#pragma once

#include <cstdint>

/// @brief Debugger protocol v2 constants.
/// Shared with Brico68k/inc/trace.inc : the values MUST be identical in both files.
/// Offsets are relative to the system ROM image; the 68k sees them at $C00000 + offset.

/// @brief Offset of the TRACE handler in the system ROM image ($C04200 for the 68k).
constexpr uint32_t BIOS_PATCH_TRACE_ROUTINE_OFFSET = 0x4200;
/// @brief Maximum size of the TRACE handler when injected into a standard BIOS (mode A only).
constexpr uint32_t BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES = 0x100;

/// @brief Pico -> 68k mailbox. $1FFFA is reserved (BricoExt / CommandManager).
/// @brief long: address (READ_MEM) / target PC (RUN_UNTIL).
constexpr uint32_t DEBUG_MAILBOX_ARG0 = 0x1FFF0;
/// @brief long: word count (READ_MEM) / minimum SP (RUN_UNTIL).
constexpr uint32_t DEBUG_MAILBOX_ARG1 = 0x1FFF4;
/// @brief word: non-zero = stop at the next passage in the handler.
constexpr uint32_t DEBUG_MAILBOX_BREAK = 0x1FFF8;
/// @brief word: command code (DebugCommand).
constexpr uint32_t DEBUG_MAILBOX_CMD = 0x1FFFC;
/// @brief word: request counter, written last by the Pico.
constexpr uint32_t DEBUG_MAILBOX_CTR = 0x1FFFE;

/// @brief Commands sent by the Pico to the 68k handler.
enum class DebugCommand : uint16_t
{
    Step = 0,      ///< Restore state, rte (T stays set)
    Continue = 1,  ///< Clear T in the stacked SR, rte
    GetRegs = 2,   ///< Send the registers response + EOT, stay in the loop
    ReadMem = 3,   ///< ARG0 = address (even), ARG1 = word count (1..256)
    RunUntil = 4   ///< ARG0 = target PC, ARG1 = minimum SP; rte with T set
};

constexpr uint16_t DEBUG_PROTOCOL_MAGIC = 0xDB02;

/// @brief Stop frame (68k -> Pico): MAGIC, SR, PC hi, PC lo, CTR, then EOT.
constexpr uint32_t DEBUG_STOP_FRAME_WORDS = 5;
/// @brief GET_REGS response: d0-d7, a0-a6, SSP_INTERRUPTED, USP (2 words each), SR, PC hi, PC lo, then EOT.
constexpr uint32_t DEBUG_REGS_RESPONSE_WORDS = 37;
/// @brief Maximum number of words for one READ_MEM.
constexpr uint32_t DEBUG_READ_MEM_MAX_WORDS = 256;
