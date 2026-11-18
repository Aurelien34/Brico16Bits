// Host tests of CallStack (call stack analyzer of the live debugger).
// Build and run : HostTests/build_callstack.sh

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "CallStack.h"

static int gnPassed = 0;
static int gnFailed = 0;

#define CHECK(condition) \
    do { \
        if (condition) { gnPassed++; } \
        else { gnFailed++; printf("  FAIL %s:%d : %s\n", __FILE__, __LINE__, #condition); } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        long long a_ = (long long)(actual), e_ = (long long)(expected); \
        if (a_ == e_) { gnPassed++; } \
        else { gnFailed++; printf("  FAIL %s:%d : %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_); } \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Synthetic 68k memory : P-ROM, banked P-ROM, work RAM, system ROM (big-endian bytes)
// ---------------------------------------------------------------------------------------------

struct Region
{
    uint32_t start;
    uint32_t size;
    uint8_t *bytes;
};

static uint8_t gaProm[0x100000];
static uint8_t gaPromBank[0x100000];
static uint8_t gaRam[0x10000];
static uint8_t gaBios[0x20000];

static Region gaRegions[] =
{
    { 0x000000, sizeof(gaProm), gaProm },
    { 0x200000, sizeof(gaPromBank), gaPromBank },
    { 0x100000, sizeof(gaRam), gaRam },
    { 0xC00000, sizeof(gaBios), gaBios },
};

static uint8_t *Locate(uint32_t address)
{
    for (Region &region : gaRegions)
    {
        if (address >= region.start && address < region.start + region.size)
        {
            return &region.bytes[address - region.start];
        }
    }
    return nullptr;
}

static void ClearMemory()
{
    for (Region &region : gaRegions)
    {
        memset(region.bytes, 0, region.size);
    }
}

static void Put16(uint32_t address, uint16_t value)
{
    uint8_t *p = Locate(address);
    if (p == nullptr || Locate(address + 1) == nullptr)
    {
        printf("  test bug : Put16 at $%06X\n", address);
        return;
    }
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void Put32(uint32_t address, uint32_t value)
{
    Put16(address, (uint16_t)(value >> 16));
    Put16(address + 2, (uint16_t)value);
}

struct ReadStats
{
    int calls;
    int words;
    uint32_t failAddress;   // Reads covering this address fail (0 = none)
};

static bool ReadMemory(uint32_t address, uint16_t words, uint16_t *buffer, void *context)
{
    ReadStats *stats = (ReadStats *)context;
    stats->calls++;
    stats->words += words;
    if ((address & 1) || words == 0)
    {
        return false;
    }
    for (uint16_t i = 0; i < words; ++i)
    {
        uint32_t a = address + 2u * i;
        if (stats->failAddress != 0 && (a == stats->failAddress || a + 1 == stats->failAddress))
        {
            return false;
        }
        uint8_t *p = Locate(a);
        if (p == nullptr)
        {
            return false;
        }
        buffer[i] = (uint16_t)((p[0] << 8) | p[1]);
    }
    return true;
}

static CallStackMemory MakeMemory(ReadStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    CallStackMemory memory;
    memory.stackBase = 0;
    memory.stackWords = nullptr;
    memory.stackWordCount = 0;
    memory.read = ReadMemory;
    memory.context = stats;
    return memory;
}

/// @brief Write a call instruction so that it ends exactly at returnAddress. Returns its length.
static void PutJsrAbsL(uint32_t returnAddress, uint32_t target)
{
    Put16(returnAddress - 6, 0x4EB9);
    Put32(returnAddress - 4, target);
}

static void PutBsrW(uint32_t returnAddress, int16_t displacement)
{
    Put16(returnAddress - 4, 0x6100);
    Put16(returnAddress - 2, (uint16_t)displacement);
}

static void PutJsrIndirect(uint32_t returnAddress, int an)
{
    Put16(returnAddress - 2, (uint16_t)(0x4E90 | an));
}

static const char *MethodName(CallerMethod method)
{
    switch (method)
    {
    case CallerMethod::Shadow: return "shadow";
    case CallerMethod::A6: return "a6";
    case CallerMethod::Scan: return "scan";
    }
    return "?";
}

static void DumpCallers(const Caller *callers, int count)
{
    for (int i = 0; i < count; ++i)
    {
        printf("    #%d $%06X sp=$%06X %s\n", i, callers[i].returnAddress, callers[i].frameSp, MethodName(callers[i].method));
    }
}

// ---------------------------------------------------------------------------------------------
// GetCallLength
// ---------------------------------------------------------------------------------------------

static uint8_t Len3(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t code[3] = { a, b, c };
    return CallStack::GetCallLength(code, 3);
}

static void TestGetCallLength()
{
    printf("GetCallLength\n");
    const uint16_t NOP = 0x4E71;

    // 2 bytes forms at -2
    CHECK_EQ(Len3(NOP, NOP, 0x4E90), 2);      // jsr (a0)
    CHECK_EQ(Len3(NOP, NOP, 0x4E97), 2);      // jsr (a7)
    CHECK_EQ(Len3(NOP, NOP, 0x6110), 2);      // bsr.s *+$12
    CHECK_EQ(Len3(NOP, NOP, 0x61FE), 2);      // bsr.s *  (negative even)
    CHECK_EQ(Len3(NOP, NOP, 0x6111), 0);      // bsr.s odd displacement
    CHECK_EQ(Len3(NOP, NOP, 0x61FF), 0);      // no BSR.L on the 68000
    CHECK_EQ(Len3(NOP, NOP, 0x6100), 0);      // bsr.w opcode at -2 : length 4, distance 2
    CHECK_EQ(Len3(NOP, NOP, 0x4ED0), 0);      // jmp (a0)
    CHECK_EQ(Len3(NOP, NOP, 0x6010), 0);      // bra.s

    // 4 bytes forms at -4
    CHECK_EQ(Len3(NOP, 0x4EA9, 0x1234), 4);   // jsr $1234(a1)
    CHECK_EQ(Len3(NOP, 0x4EAE, 0xFFE2), 4);   // jsr -30(a6) (odd displacements allowed with An)
    CHECK_EQ(Len3(NOP, 0x4EB0, 0x1004), 4);   // jsr 4(a0,d1.w)
    CHECK_EQ(Len3(NOP, 0x4EB0, 0x1104), 0);   // bit 8 set : not a 68000 brief extension word
    CHECK_EQ(Len3(NOP, 0x4EB8, 0x0400), 4);   // jsr $0400.w
    CHECK_EQ(Len3(NOP, 0x4EB8, 0x0401), 0);   // odd target
    CHECK_EQ(Len3(NOP, 0x4EBA, 0x0100), 4);   // jsr *+$102(pc)
    CHECK_EQ(Len3(NOP, 0x4EBA, 0x0101), 0);   // odd target
    CHECK_EQ(Len3(NOP, 0x4EBB, 0x0006), 4);   // jsr 6(pc,d0.w)
    CHECK_EQ(Len3(NOP, 0x4EBB, 0x0606), 0);   // scale bits set
    CHECK_EQ(Len3(NOP, 0x6100, 0x0100), 4);   // bsr.w
    CHECK_EQ(Len3(NOP, 0x6100, 0x0000), 4);   // bsr.w with zero displacement
    CHECK_EQ(Len3(NOP, 0x6100, 0x0101), 0);   // bsr.w odd displacement
    CHECK_EQ(Len3(NOP, 0x4E90, 0x1234), 0);   // jsr (a0) at -4 : length 2, distance 4
    CHECK_EQ(Len3(NOP, 0x4EB9, 0x0000), 0);   // jsr abs.L at -4 : length 6, distance 4
    CHECK_EQ(Len3(NOP, 0x4EF9, 0x0000), 0);   // jmp abs.W-like opcode

    // 6 bytes forms at -6
    CHECK_EQ(Len3(0x4EB9, 0x0001, 0x2340), 6);   // jsr $12340
    CHECK_EQ(Len3(0x4EB9, 0x00C0, 0x0402), 6);   // jsr $C00402
    CHECK_EQ(Len3(0x4EB9, 0x0001, 0x2341), 0);   // odd target
    CHECK_EQ(Len3(0x4EB9, 0x0100, 0x0000), 0);   // beyond 24 bits
    CHECK_EQ(Len3(0x4EF9, 0x0001, 0x2340), 0);   // jmp abs.L

    // Fewer words available
    uint16_t one[1] = { 0x4EB9 };
    CHECK_EQ(CallStack::GetCallLength(one, 1), 0);
    uint16_t two[2] = { 0x4EB9, 0x4E91 };
    CHECK_EQ(CallStack::GetCallLength(two, 2), 2);
    CHECK_EQ(CallStack::GetCallLength(nullptr, 3), 0);

    // Code zones
    CHECK(CallStack::IsInCodeZone(0x001000));
    CHECK(!CallStack::IsInCodeZone(0x001001));      // odd
    CHECK(!CallStack::IsInCodeZone(0x000080));      // vector table
    CHECK(!CallStack::IsInCodeZone(0x100400));      // RAM
    CHECK(CallStack::IsInCodeZone(0x2FFFFE));
    CHECK(!CallStack::IsInCodeZone(0x300000));
    CHECK(CallStack::IsInCodeZone(0xC1FFFE));
    CHECK(!CallStack::IsInCodeZone(0xC20000));
    CHECK(!CallStack::IsInCodeZone(0xC00000));      // no room for an opcode before it
}

// ---------------------------------------------------------------------------------------------
// Method 1 : shadow stack
// ---------------------------------------------------------------------------------------------

static void TestShadowStack()
{
    printf("Shadow stack\n");
    ClearMemory();
    ReadStats stats;
    CallStackMemory memory = MakeMemory(&stats);
    memory.read = nullptr;   // The shadow stack needs no memory at all
    Caller callers[CALLSTACK_MAX_CALLERS];
    CallStack stack;

    // main at $1000 : jsr $2000 (6 bytes), SP $10F000 -> $10EFFC
    stack.OnStep(0x1000, 0x10F000, InstructionFlow::Call, 6, 0x2000, 0x10EFFC);
    CHECK_EQ(stack.GetShadowDepth(), 1);
    // Some instructions in the callee : push a register, then bsr.s (2 bytes)
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Other, 2, 0x2002, 0x10EFF8);
    stack.OnStep(0x2002, 0x10EFF8, InstructionFlow::Call, 2, 0x2100, 0x10EFF4);
    CHECK_EQ(stack.GetShadowDepth(), 2);
    // DBcc in the inner function
    stack.OnStep(0x2100, 0x10EFF4, InstructionFlow::Loop, 4, 0x2100, 0x10EFF4);

    int count = stack.Analyze(0x10EFF4, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 2);
    CHECK_EQ(callers[0].returnAddress, 0x2004);
    CHECK_EQ(callers[0].frameSp, 0x10EFF4);
    CHECK(callers[0].method == CallerMethod::Shadow);
    CHECK_EQ(callers[1].returnAddress, 0x1006);
    CHECK_EQ(callers[1].frameSp, 0x10EFFC);
    CHECK_EQ(stack.GetLastReadCount(), 0);

    // maxCallers = 1
    CHECK_EQ(stack.Analyze(0x10EFF4, 0, memory, callers, 1), 1);
    CHECK_EQ(callers[0].returnAddress, 0x2004);

    // RTS of the inner function : SP $10EFF4 -> $10EFF8 pops exactly the matching entry
    stack.OnStep(0x2104, 0x10EFF4, InstructionFlow::Return, 2, 0x2004, 0x10EFF8);
    CHECK_EQ(stack.GetShadowDepth(), 1);
    count = stack.Analyze(0x10EFF8, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK_EQ(callers[0].returnAddress, 0x1006);

    // pea $3000 ; rts (computed jump) : the slot is below the tracked frame, nothing is popped
    stack.OnStep(0x2004, 0x10EFF8, InstructionFlow::Other, 4, 0x2008, 0x10EFF4);
    stack.OnStep(0x2008, 0x10EFF4, InstructionFlow::Return, 2, 0x3000, 0x10EFF8);
    CHECK_EQ(stack.GetShadowDepth(), 1);

    // Pop the saved register, RTS to main
    stack.OnStep(0x3000, 0x10EFF8, InstructionFlow::Other, 2, 0x3002, 0x10EFFC);
    CHECK_EQ(stack.GetShadowDepth(), 1);
    stack.OnStep(0x3002, 0x10EFFC, InstructionFlow::Return, 2, 0x1006, 0x10F000);
    CHECK_EQ(stack.GetShadowDepth(), 0);

    // TRAP #0 (2 bytes) : SR + PC pushed (6 bytes), return address slot at newSp + 2
    stack.OnStep(0x1006, 0x10F000, InstructionFlow::Trap, 2, 0xC00800, 0x10EFFA);
    CHECK_EQ(stack.GetShadowDepth(), 1);
    count = stack.Analyze(0x10EFFA, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK_EQ(callers[0].returnAddress, 0x1008);
    CHECK_EQ(callers[0].frameSp, 0x10EFFC);   // Step out : RUN_UNTIL($1008, $10F000)
    // RTE : SP + 6
    stack.OnStep(0xC00810, 0x10EFFA, InstructionFlow::Return, 2, 0x1008, 0x10F000);
    CHECK_EQ(stack.GetShadowDepth(), 0);

    // TRAPV not taken : no change, no invalidation
    stack.OnStep(0x1008, 0x10F000, InstructionFlow::Call, 4, 0x2000, 0x10EFFC);
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Trap, 2, 0x2002, 0x10EFFC);
    CHECK_EQ(stack.GetShadowDepth(), 1);

    // RTR : subroutine pushed SR (move sr,-(sp)), RTR pops CCR + PC
    stack.OnStep(0x2002, 0x10EFFC, InstructionFlow::Other, 2, 0x2004, 0x10EFFA);
    CHECK_EQ(stack.GetShadowDepth(), 1);
    stack.OnStep(0x2004, 0x10EFFA, InstructionFlow::Return, 2, 0x100C, 0x10F000);
    CHECK_EQ(stack.GetShadowDepth(), 0);

    // Stack unwinding without return : addq.l #4,sp then jmp
    stack.OnStep(0x100C, 0x10F000, InstructionFlow::Call, 4, 0x2000, 0x10EFFC);
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Other, 2, 0x2002, 0x10F000);
    CHECK_EQ(stack.GetShadowDepth(), 0);

    // Unexpected stack effect for a call : invalidates
    stack.OnStep(0x1000, 0x10F000, InstructionFlow::Call, 6, 0x2000, 0x10EFFC);
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Call, 4, 0x3000, 0x10EFF0);
    CHECK_EQ(stack.GetShadowDepth(), 0);
    // TRAP in user mode (SP switches to SSP) : invalidates
    stack.OnStep(0x1000, 0x10F000, InstructionFlow::Call, 6, 0x2000, 0x10EFFC);
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Trap, 2, 0xC00800, 0x10FF00);
    CHECK_EQ(stack.GetShadowDepth(), 0);
    // Odd size : invalidates
    stack.OnStep(0x1000, 0x10F000, InstructionFlow::Call, 6, 0x2000, 0x10EFFC);
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Call, 3, 0x3000, 0x10EFF8);
    CHECK_EQ(stack.GetShadowDepth(), 0);

    // Step over (RUN_UNTIL reached at the same SP) keeps the frames, step out releases one
    stack.OnStep(0x1000, 0x10F000, InstructionFlow::Call, 6, 0x2000, 0x10EFFC);
    stack.OnStep(0x2000, 0x10EFFC, InstructionFlow::Call, 4, 0x3000, 0x10EFF8);
    stack.OnRunUntilReached(0x10EFF8);   // step over in $3000
    CHECK_EQ(stack.GetShadowDepth(), 2);
    count = stack.Analyze(0x10EFF8, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(callers[0].frameSp + 4, 0x10EFFC);
    stack.OnRunUntilReached(callers[0].frameSp + 4);   // step out to $2004
    CHECK_EQ(stack.GetShadowDepth(), 1);
    count = stack.Analyze(0x10EFFC, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK_EQ(callers[0].returnAddress, 0x1006);

    // Slot reuse : a new call at the same SP as a stale (untracked-pop) entry replaces it
    stack.OnStep(0x5000, 0x10F000, InstructionFlow::Call, 2, 0x6000, 0x10EFFC);
    CHECK_EQ(stack.GetShadowDepth(), 1);
    count = stack.Analyze(0x10EFFC, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK_EQ(callers[0].returnAddress, 0x5002);

    // Invalidate
    stack.Invalidate();
    CHECK_EQ(stack.GetShadowDepth(), 0);
    CHECK_EQ(stack.Analyze(0x10EFFC, 0, memory, callers, CALLSTACK_MAX_CALLERS), 0);

    // Overflow : the innermost frames are kept
    uint32_t sp = 0x10F000;
    for (int i = 0; i < CALLSTACK_SHADOW_DEPTH + 8; ++i)
    {
        stack.OnStep(0x1000 + 0x10 * i, sp, InstructionFlow::Call, 4, 0x1000 + 0x10 * (i + 1), sp - 4);
        sp -= 4;
    }
    CHECK_EQ(stack.GetShadowDepth(), CALLSTACK_SHADOW_DEPTH);
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(callers[0].returnAddress, 0x1000 + 0x10 * (CALLSTACK_SHADOW_DEPTH + 7) + 4);
    CHECK_EQ(callers[0].frameSp, sp);
    // Release everything
    stack.OnStep(0x9000, sp, InstructionFlow::Return, 2, 0x9000, 0x10F000);
    CHECK_EQ(stack.GetShadowDepth(), 0);
}

// ---------------------------------------------------------------------------------------------
// Method 2 : A6 chain
// ---------------------------------------------------------------------------------------------

/// @brief Build a LINK frame at `frame` : (frame) = previous, 4(frame) = return address
static void PutFrame(uint32_t frame, uint32_t previous, uint32_t returnAddress)
{
    Put32(frame, previous);
    Put32(frame + 4, returnAddress);
}

static void TestA6Chain()
{
    printf("A6 chain\n");
    ClearMemory();
    ReadStats stats;
    CallStackMemory memory = MakeMemory(&stats);
    Caller callers[CALLSTACK_MAX_CALLERS];
    CallStack stack;

    // Three C functions : main <- f1 <- f2 <- f3 (current)
    PutJsrAbsL(0x001206, 0x2000);
    PutBsrW(0x002104, 0x0100);
    PutJsrIndirect(0x002302, 2);
    PutFrame(0x10EF00, 0x10EF40, 0x002302);   // f3's frame, return into f2
    PutFrame(0x10EF40, 0x10EF80, 0x002104);   // f2's frame, return into f1
    PutFrame(0x10EF80, 0x000000, 0x001206);   // f1's frame, return into main, end of chain
    uint32_t sp = 0x10EEF0;

    int count = stack.Analyze(sp, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    DumpCallers(callers, count);
    CHECK(count >= 3);
    CHECK_EQ(callers[0].returnAddress, 0x002302);
    CHECK_EQ(callers[0].frameSp, 0x10EF04);
    CHECK(callers[0].method == CallerMethod::A6);
    CHECK_EQ(callers[1].returnAddress, 0x002104);
    CHECK(callers[1].method == CallerMethod::A6);
    CHECK_EQ(callers[2].returnAddress, 0x001206);
    CHECK_EQ(callers[2].frameSp, 0x10EF84);
    CHECK(callers[2].method == CallerMethod::A6);
    CHECK_EQ(count, 3);   // Nothing else above (the scan continues above $10EF88 and finds zeros)
    // Reads : stack window (1) + 3 code reads ; A6 links are inside the 128 words window
    CHECK_EQ(stack.GetLastReadCount(), 4);
    CHECK_EQ(stats.calls, 4);

    // Second analysis : verdicts are cached, only the stack window is read
    stats.calls = 0;
    count = stack.Analyze(sp, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 3);
    CHECK_EQ(stats.calls, 1);

    // Odd A6 : no A6 caller, the scan finds the same return addresses
    count = stack.Analyze(sp, 0x10EF01, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 3);
    CHECK(callers[0].method == CallerMethod::Scan);
    CHECK_EQ(callers[0].frameSp, 0x10EF04);

    // A6 below SP : rejected
    count = stack.Analyze(0x10EF10, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK(count >= 1);
    CHECK(callers[0].method == CallerMethod::Scan);
    CHECK_EQ(callers[0].returnAddress, 0x002104);

    // A6 outside RAM : rejected
    count = stack.Analyze(sp, 0x0FFF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK(callers[0].method == CallerMethod::Scan);
    count = stack.Analyze(sp, 0xFFFFFFFC, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK(callers[0].method == CallerMethod::Scan);

    // Non increasing chain : f2's frame points back to a lower address
    PutFrame(0x10EF40, 0x10EF20, 0x002104);
    count = stack.Analyze(sp, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(callers[0].method == CallerMethod::A6, true);
    CHECK_EQ(callers[1].method == CallerMethod::A6, true);
    CHECK(count < 3 || callers[2].method == CallerMethod::Scan);   // Chain stopped after 2 links

    // Cycle : frame pointing to itself
    PutFrame(0x10EF40, 0x10EF40, 0x002104);
    count = stack.Analyze(sp, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(callers[0].returnAddress, 0x002302);
    CHECK_EQ(callers[1].returnAddress, 0x002104);
    CHECK(callers[1].method == CallerMethod::A6);
    CHECK(count < 3 || callers[2].method == CallerMethod::Scan);
    // Overlapping frames (+4 only) are rejected too
    PutFrame(0x10EF40, 0x10EF44, 0x002104);
    count = stack.Analyze(sp, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK(count < 3 || callers[2].method == CallerMethod::Scan);
    PutFrame(0x10EF40, 0x10EF80, 0x002104);

    // Return address not preceded by a call : the chain stops at that link
    stack.ClearCodeCache();
    PutFrame(0x10EF00, 0x10EF40, 0x002400);   // $2400 : preceded by zeros
    count = stack.Analyze(sp, 0x10EF00, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK(callers[0].method == CallerMethod::Scan);
    CHECK_EQ(callers[0].returnAddress, 0x002104);   // The scan skips $2400 too
    PutFrame(0x10EF00, 0x10EF40, 0x002302);

    // A6 frames outside the stack window : read through the callback (4 words per link)
    ClearMemory();
    stack.ClearCodeCache();
    PutJsrAbsL(0x001206, 0x2000);
    PutFrame(0x10F800, 0x000000, 0x001206);
    memset(&stats, 0, sizeof(stats));
    count = stack.Analyze(0x10EEF0, 0x10F800, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK(callers[0].method == CallerMethod::A6);
    CHECK_EQ(stats.calls, 3);   // window + link + code

    // Depth limit : 20 frames, at most CALLSTACK_A6_MAX_DEPTH links followed
    ClearMemory();
    stack.ClearCodeCache();
    for (int i = 0; i < 20; ++i)
    {
        uint32_t returnAddress = 0x003000 + 0x10 * i;
        PutJsrIndirect(returnAddress, 0);
        PutFrame(0x10E000 + 0x100 * i, 0x10E000 + 0x100 * (i + 1), returnAddress);
    }
    count = stack.Analyze(0x10DFF0, 0x10E000, memory, callers, CALLSTACK_MAX_CALLERS);
    int a6Count = 0;
    for (int i = 0; i < count; ++i)
    {
        a6Count += callers[i].method == CallerMethod::A6;
    }
    CHECK_EQ(a6Count, CALLSTACK_A6_MAX_DEPTH);
    CHECK_EQ(callers[15].returnAddress, 0x003000 + 0x10 * 15);
}

// ---------------------------------------------------------------------------------------------
// Method 3 : stack scan
// ---------------------------------------------------------------------------------------------

static void TestScan()
{
    printf("Stack scan\n");
    ClearMemory();
    ReadStats stats;
    CallStackMemory memory = MakeMemory(&stats);
    Caller callers[CALLSTACK_MAX_CALLERS];
    CallStack stack;

    // Code
    PutJsrAbsL(0x001206, 0x2000);          // P-ROM, jsr abs.L
    PutBsrW(0xC00504, 0x0040);             // BIOS, bsr.w
    PutJsrIndirect(0x200102, 1);           // banked P-ROM, jsr (a1)
    Put16(0x001302, 0x4ED0);               // jmp (a0) before $1304 : false positive
    Put16(0x001400, 0x4E90);               // jsr (a0) at -4 of $1404 : wrong distance
    Put16(0x001402, 0x4E71);

    // Stack, SP = $10F000
    uint32_t sp = 0x10F000;
    Put32(sp + 0x00, 0x00000001);   // data
    Put32(sp + 0x04, 0x00001304);   // even, P-ROM, preceded by jmp : rejected
    Put32(sp + 0x08, 0x00001207);   // odd : rejected without read
    Put16(sp + 0x0C, 0x1234);       // misaligned long : ($1234,$0000) then ($0000,$1206)
    Put32(sp + 0x0E, 0x00001206);   // valid, at a word (not long) aligned offset
    Put32(sp + 0x12, 0x00400000);   // outside the code zones : rejected without read
    Put32(sp + 0x16, 0x00000080);   // vector table : rejected without read
    Put32(sp + 0x1A, 0x00001404);   // jsr (a0) at the wrong distance : rejected
    Put32(sp + 0x1E, 0x00C00504);   // valid, BIOS
    Put32(sp + 0x22, 0x00200102);   // valid, banked
    Put32(sp + 0x100, 0x00001206);  // word 128 from SP : beyond the scanned window

    int count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    DumpCallers(callers, count);
    CHECK_EQ(count, 3);
    CHECK_EQ(callers[0].returnAddress, 0x001206);
    CHECK_EQ(callers[0].frameSp, sp + 0x0E);
    CHECK(callers[0].method == CallerMethod::Scan);
    CHECK_EQ(callers[1].returnAddress, 0xC00504);
    CHECK_EQ(callers[1].frameSp, sp + 0x1E);
    CHECK_EQ(callers[2].returnAddress, 0x200102);
    int firstReads = stats.calls;
    printf("    reads : first analysis %d\n", firstReads);
    // window + $10000 (misaligned long $0001:$0000 at SP + 2, even and in the P-ROM zone)
    // + $1304 + $1206 + $1404 + $C00504 + $200102 ; odd / out of zone longs cost no read
    CHECK_EQ(firstReads, 7);

    // Second analysis : non banked verdicts are cached, the banked one is read again
    stats.calls = 0;
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 3);
    CHECK_EQ(stats.calls, 2);   // window + $200102

    // maxCallers bounds the reads
    stack.ClearCodeCache();
    stats.calls = 0;
    count = stack.Analyze(sp, 0, memory, callers, 1);
    CHECK_EQ(count, 1);
    CHECK_EQ(stats.calls, 4);   // window + $10000 + $1304 + $1206

    // Window supplied by the caller : no stack read
    uint16_t window[CALLSTACK_SCAN_MAX_WORDS];
    ReadStats dummy;
    memset(&dummy, 0, sizeof(dummy));
    ReadMemory(sp, CALLSTACK_SCAN_MAX_WORDS, window, &dummy);
    memory.stackBase = sp;
    memory.stackWords = window;
    memory.stackWordCount = CALLSTACK_SCAN_MAX_WORDS;
    stats.calls = 0;
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 3);
    CHECK_EQ(stats.calls, 3);   // no stack read ; $1404, $C00504 (not cached yet) and $200102 (banked)

    // No callback at all : only the window, code reads fail -> nothing found, no crash
    memory.read = nullptr;
    stack.ClearCodeCache();
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 0);
    memory.read = ReadMemory;
    memory.stackWords = nullptr;

    // Failed code read : candidate rejected and not cached
    stack.ClearCodeCache();
    stats.failAddress = 0x001200;
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 2);
    CHECK_EQ(callers[0].returnAddress, 0xC00504);
    stats.failAddress = 0;
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 3);

    // Return address at the very beginning of a zone : only the words inside the zone are read
    ClearMemory();
    stack.ClearCodeCache();
    Put16(0x200000, 0x4E91);                // jsr (a1) at $200000, return address $200002
    Put32(sp, 0x00200002);
    count = stack.Analyze(sp, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK_EQ(callers[0].returnAddress, 0x200002);

    // Stack near the end of the work RAM : window clipped, no read beyond $10FFFF
    ClearMemory();
    stack.ClearCodeCache();
    PutJsrAbsL(0x001206, 0x2000);
    Put32(0x10FFF8, 0x00001206);
    memset(&stats, 0, sizeof(stats));
    count = stack.Analyze(0x10FFF0, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 1);
    CHECK_EQ(callers[0].frameSp, 0x10FFF8);
    CHECK_EQ(stats.words, 8 + 3);

    // SP outside RAM : nothing read for the stack
    memset(&stats, 0, sizeof(stats));
    count = stack.Analyze(0x200000, 0, memory, callers, CALLSTACK_MAX_CALLERS);
    CHECK_EQ(count, 0);
    CHECK_EQ(stats.calls, 0);
}

// ---------------------------------------------------------------------------------------------
// Priority and combination of the methods
// ---------------------------------------------------------------------------------------------

static void TestPriority()
{
    printf("Priority\n");
    ClearMemory();
    ReadStats stats;
    CallStackMemory memory = MakeMemory(&stats);
    Caller callers[CALLSTACK_MAX_CALLERS];
    CallStack stack;

    // Outer frames (before the break) : main <- f1, known by the stack only
    PutJsrAbsL(0x001206, 0x2000);   // main -> f1
    PutBsrW(0x002104, 0x0100);      // f1 -> f2 (C, LINK a6)
    PutJsrIndirect(0x002202, 0);    // f2 -> f3 (asm, observed by the shadow stack)
    Put32(0x10EFFC, 0x00001206);            // main's call to f1 (no frame pointer in f1)
    PutFrame(0x10EFE8, 0x00000000, 0x002104); // f2's LINK frame, slot $10EFEC
    // Break in f2 at SP $10EFE0, then a traced jsr (a0) to f3
    stack.Invalidate();
    stack.OnStep(0x002200, 0x10EFE0, InstructionFlow::Call, 2, 0x003000, 0x10EFDC);
    Put32(0x10EFDC, 0x00002202);
    // f3 pushes two registers and makes a6 point to f2's frame (unchanged)
    stack.OnStep(0x003000, 0x10EFDC, InstructionFlow::Other, 4, 0x003004, 0x10EFD4);

    int count = stack.Analyze(0x10EFD4, 0x10EFE8, memory, callers, CALLSTACK_MAX_CALLERS);
    DumpCallers(callers, count);
    CHECK_EQ(count, 3);
    CHECK_EQ(callers[0].returnAddress, 0x002202);
    CHECK(callers[0].method == CallerMethod::Shadow);
    CHECK_EQ(callers[1].returnAddress, 0x002104);
    CHECK(callers[1].method == CallerMethod::A6);
    CHECK_EQ(callers[2].returnAddress, 0x001206);
    CHECK(callers[2].method == CallerMethod::Scan);
    CHECK_EQ(callers[2].frameSp, 0x10EFFC);

    // The A6 link that describes the same frame as a shadow entry is skipped, not duplicated
    // f2 itself : call observed, then LINK a6 -> a6 = slot - 4
    stack.Invalidate();
    ClearMemory();
    PutJsrAbsL(0x001206, 0x2000);
    PutBsrW(0x002104, 0x0100);
    PutFrame(0x10EF40, 0x00000000, 0x001206);   // f1's frame
    stack.OnStep(0x002100, 0x10EF10, InstructionFlow::Call, 4, 0x002200, 0x10EF0C);
    Put32(0x10EF0C, 0x00002104);
    // link a6,#-4 : push a6 at $10EF08, a6 = $10EF08, sp = $10EF04
    Put32(0x10EF08, 0x0010EF40);
    stack.OnStep(0x002200, 0x10EF0C, InstructionFlow::Other, 4, 0x002204, 0x10EF04);
    count = stack.Analyze(0x10EF04, 0x10EF08, memory, callers, CALLSTACK_MAX_CALLERS);
    DumpCallers(callers, count);
    CHECK_EQ(count, 2);
    CHECK_EQ(callers[0].returnAddress, 0x002104);
    CHECK(callers[0].method == CallerMethod::Shadow);
    CHECK_EQ(callers[1].returnAddress, 0x001206);
    CHECK(callers[1].method == CallerMethod::A6);
    CHECK_EQ(callers[1].frameSp, 0x10EF44);   // f1's frame slot
}

int main()
{
    TestGetCallLength();
    TestShadowStack();
    TestA6Chain();
    TestScan();
    TestPriority();
    printf("\n%d passed, %d failed\n", gnPassed, gnFailed);
    return gnFailed == 0 ? 0 : 1;
}
