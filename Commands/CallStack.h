#pragma once

#include <cstdint>
#include "InstructionFlow.h"

/// @brief Maximum number of callers returned by CallStack::Analyze().
#define CALLSTACK_MAX_CALLERS 16
/// @brief Capacity of the shadow stack kept by the Pico (the oldest entries are dropped on overflow).
#define CALLSTACK_SHADOW_DEPTH 32
/// @brief Maximum number of links followed in the A6 chain.
#define CALLSTACK_A6_MAX_DEPTH 16
/// @brief Number of stack words scanned from SP by the heuristic (method 3).
#define CALLSTACK_SCAN_MAX_WORDS 128
/// @brief Number of call site verdicts kept across stops (non banked code only).
#define CALLSTACK_CODE_CACHE_SIZE 32

/// @brief Method that found a caller, by decreasing reliability.
enum class CallerMethod : uint8_t
{
    Shadow,  ///< Shadow stack kept by the Pico while stepping (exact)
    A6,      ///< A6 frame chain built by LINK (compiled code)
    Scan     ///< Stack scan: long preceded by a valid JSR / BSR (heuristic)
};

/// @brief One caller of the current code.
struct Caller
{
    uint32_t returnAddress;  ///< Address the caller returns to (instruction after its JSR / BSR / TRAP)
    uint32_t frameSp;        ///< Address of the stack slot holding returnAddress. Step out:
                             ///< RUN_UNTIL(returnAddress, frameSp + 4)
    CallerMethod method;     ///< Method that found this caller
};

/// @brief Memory read callback : read `words` big-endian 16 bits words at the even `address`
/// (as seen by the 68k) into buffer[0..words-1]. Returns false if the read failed.
/// Each call on the real hardware is one READ_MEM round-trip through the 68k, except where the
/// Pico can read memory directly (system ROM image, $C00000-$C1FFFF).
typedef bool (*CallStackReadCallback)(uint32_t address, uint16_t words, uint16_t *buffer, void *context);

/// @brief Memory available to CallStack::Analyze().
struct CallStackMemory
{
    /// @brief Optional pre-fetched stack window: stackWords[i] is the word at stackBase + 2 * i.
    /// Normally stackBase = SP and up to CALLSTACK_SCAN_MAX_WORDS words. If stackWords is nullptr,
    /// Analyze() reads the window itself with `read` (one call, at most CALLSTACK_SCAN_MAX_WORDS
    /// words, clipped to the end of the work RAM $10FFFF).
    uint32_t stackBase;
    const uint16_t *stackWords;
    uint16_t stackWordCount;
    /// @brief Callback for everything outside the window (A6 links, code before return addresses).
    /// May be nullptr: such reads then fail (the corresponding candidates are rejected).
    CallStackReadCallback read;
    void *context;
};

/// @brief Call stack analyzer of the live debugger (pure logic, no hardware access).
///
/// Three methods, used in this priority order, each one continuing above the frames found by
/// the previous one (so the result is ordered from the innermost caller to the outermost):
///  1. Shadow stack, kept by the Pico while stepping: OnStep() pushes the return address on a
///     taken Call / Trap and drops every entry whose slot is below the new SP (RTS, RTR, RTE,
///     stack unwinding...). Its entries are exact as long as the rules of "Validity" are followed.
///  2. A6 chain: (A6) = previous A6, 4(A6) = return address. Each link must be even, in work RAM
///     $100000-$10FFFF, >= SP for the first one, then strictly increasing (at least +8), at most
///     CALLSTACK_A6_MAX_DEPTH links; its return address must pass the same check as method 3.
///  3. Stack scan over CALLSTACK_SCAN_MAX_WORDS words from SP: a long is retained if it is even,
///     in a code zone ($000100-$0FFFFF, $200000-$2FFFFF, $C00000-$C1FFFF) and preceded by a
///     valid JSR / BSR whose length matches the distance (see GetCallLength()).
///
/// Validity of the shadow stack: every entry it holds is exact, provided that after EVERY stop
/// exactly one of these is called:
///  - OnStep() after a STEP (single instruction traced);
///  - OnRunUntilReached() after a RUN_UNTIL used for step over or step out that stopped on its
///    target PC with SP >= its minimum SP (the code run in between has a zero net effect on the
///    frames below the target SP, frames above are released by the SP rule);
///  - Invalidate() in every other case: CONTINUE then break, RUN_UNTIL to an arbitrary address
///    ("g"), RUN_UNTIL stopped by a break (or anywhere but its target), new session / 68k reset,
///    any link error that leaves the previous SP or the new SP unknown, register or memory edit.
/// After Invalidate() the shadow stack is empty and fills again with the next observed calls;
/// frames older than its oldest entry are then found by methods 2 and 3.
/// Limitation: the effective a7 must stay on the same stack (supervisor mode, as on the Neo Geo);
/// a TRAP taken in user mode switches to SSP and invalidates the shadow stack.
class CallStack
{
public:
    CallStack();

    /// @brief Invalidate the shadow stack and clear the code verdict cache (new session).
    void Reset();

    /// @brief Invalidate the shadow stack (see "Validity"). The code verdict cache is kept.
    void Invalidate();

    /// @brief Forget the cached call site verdicts (code changed : new cartridge, reset...).
    void ClearCodeCache();

    /// @brief Update the shadow stack after a STEP.
    /// @param prevPc PC of the instruction that has just been executed
    /// @param prevSp Effective a7 before that instruction
    /// @param flow Category of that instruction
    /// @param size Size of that instruction in bytes (return address = prevPc + size)
    /// @param newPc PC at the new stop
    /// @param newSp Effective a7 at the new stop
    /// Call : expects newSp == prevSp - 4. Trap : expects newSp == prevSp - 6, or newSp == prevSp and
    /// newPc == prevPc + size (TRAPV not taken). Anything else for a Call / Trap invalidates.
    /// All flows : every entry whose slot is below newSp is dropped (Return, stack unwinding).
    void OnStep(uint32_t prevPc, uint32_t prevSp, InstructionFlow flow, uint8_t size, uint32_t newPc, uint32_t newSp);

    /// @brief Update the shadow stack after a RUN_UNTIL step over / step out that reached its target.
    void OnRunUntilReached(uint32_t newSp);

    /// @brief Number of entries in the shadow stack.
    int GetShadowDepth() const { return mnShadowCount; }

    /// @brief Build the list of callers, innermost first.
    /// @param sp Effective a7 at the stop
    /// @param a6 Register a6 at the stop
    /// @param memory Stack window and read callback
    /// @param callers Receives the callers
    /// @param maxCallers Size of callers (clipped to CALLSTACK_MAX_CALLERS). The analysis stops as
    /// soon as it is reached, which also bounds the number of reads.
    /// @return Number of callers written
    int Analyze(uint32_t sp, uint32_t a6, const CallStackMemory &memory, Caller *callers, int maxCallers);

    /// @brief Number of read callback calls made by the last Analyze() (diagnostic, tests).
    int GetLastReadCount() const { return mnLastReadCount; }

    /// @brief Length in bytes (2, 4 or 6) of a valid JSR / BSR ending exactly at a return address,
    /// or 0 if there is none.
    /// @param code Words preceding the return address : code[wordCount - 1] is the word at
    /// returnAddress - 2, code[0] the word at returnAddress - 2 * wordCount
    /// @param wordCount 1 to 3 (fewer words only allow the shorter forms)
    /// Accepted : JSR (An) [-2], d16(An) / d8(An,Xn) / abs.W / d16(PC) / d8(PC,Xn) [-4],
    /// abs.L [-6]; BSR.S [-2] (even non zero displacement), BSR.W [-4] (even displacement).
    /// The 68000 has no BSR.L ($61FF is an odd BSR.S displacement : rejected).
    static uint8_t GetCallLength(const uint16_t *code, int wordCount);

    /// @brief True if the address is even and inside a code zone, with room for an opcode before it.
    static bool IsInCodeZone(uint32_t address);

private:
    struct ShadowEntry
    {
        uint32_t returnAddress;
        uint32_t frameSp;
    };

    struct CodeVerdict
    {
        uint32_t returnAddress;
        bool valid;
    };

    void ReleaseFrames(uint32_t newSp);
    void PushFrame(uint32_t returnAddress, uint32_t frameSp);
    bool ReadWords(uint32_t address, uint16_t words, uint16_t *buffer);
    bool IsReturnAddress(uint32_t address);
    bool AddCaller(uint32_t returnAddress, uint32_t frameSp, CallerMethod method);

    ShadowEntry maShadow[CALLSTACK_SHADOW_DEPTH];  ///< [0] = oldest, [count - 1] = innermost (lowest frameSp)
    int mnShadowCount;

    CodeVerdict maCodeCache[CALLSTACK_CODE_CACHE_SIZE];
    int mnCodeCacheCount;
    int mnCodeCacheNext;

    // Analyze() working state
    const CallStackMemory *mpMemory;
    uint32_t mnWindowBase;
    const uint16_t *mpWindow;
    uint16_t mnWindowWords;
    uint16_t maStackBuffer[CALLSTACK_SCAN_MAX_WORDS];
    Caller *mpCallers;
    int mnCallerCount;
    int mnMaxCallers;
    int mnLastReadCount;
};
