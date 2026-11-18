#pragma once

#include <cstdint>
#include "DebugFrame.h"

/// @brief Parses address expressions typed in the debugger prompt
///
/// Grammar: base [ ('+'|'-') hexnumber ]
///   base      = hexnumber | register
///   hexnumber = ["$" | "0x"] hexdigits   (case insensitive, at most 32 bits)
///   register  = d0..d7 | a0..a7 | sp (= a7) | pc   (case insensitive)
/// A name that is a valid register ("d0", "a7"...) is always a register, never a bare hex number.
class DebugExpression
{
public:
    /// @brief Evaluate an expression
    /// @param text NUL-terminated expression (leading/trailing spaces ignored)
    /// @param frame Source of register values, may be null (registers are then rejected)
    /// @param value Receives the result on success, untouched on failure
    /// @return true if the whole text is a valid expression and the result fits in 32 bits
    static bool Parse(const char *text, const DebugFrame *frame, uint32_t *value);
};
