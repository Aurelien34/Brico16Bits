// Host tests of GetBranchTarget (absolute destination of relative branches in the live debugger disassembly).
// Build and run : HostTests/build_branchtarget.sh

#include <cstdio>
#include <cstdint>
#include <initializer_list>

#include "DebugFrame.h"

static int gnPassed = 0;
static int gnFailed = 0;

static const uint32_t NONE = 0xffffffffu;

/// @brief Expect a target equal to expected (or no target if expected is NONE)
static void Expect(int line, uint32_t address, uint32_t expected, std::initializer_list<uint16_t> list)
{
    uint16_t words[8] = {0};
    int count = 0;
    for (uint16_t w : list)
    {
        words[count++] = w;
    }
    uint32_t target = 0x12345678;
    bool has = GetBranchTarget(address, words, count, &target);
    bool ok = expected == NONE ? !has : (has && target == expected);
    if (ok) { gnPassed++; }
    else { gnFailed++; printf("  FAIL line %d : has=%d target=%06lx expected=%06lx\n", line, has, (unsigned long)target, (unsigned long)expected); }
}
#define EXPECT(address, expected, ...) Expect(__LINE__, address, expected, { __VA_ARGS__ })

int main()
{
    EXPECT(0xc007a8, 0xc0078c, 0x67e2);             // beq.s -30
    EXPECT(0x1000, 0x1006, 0x6004);                 // bra.s +4
    EXPECT(0x1000, 0x1002 - 1822, 0x6000, 0xf8e2);  // bra.w -1822
    EXPECT(0x1000, 0x1012, 0x6100, 0x0010);         // bsr.w +16
    EXPECT(0x1000, NONE, 0x6100);                   // displacement word missing
    EXPECT(0x1000, NONE, 0x60ff, 0xffff, 0xfff0);   // $FF: 32-bit displacement, not valid on 68000
    EXPECT(0x1000, 0x1000, 0x66fe);                 // bne.s -2: loops on itself
    EXPECT(0x1000, 0x1002 - 4, 0x51c8, 0xfffc);     // dbra d0,-4
    EXPECT(0x1000, NONE, 0x51c8);                   // DBcc, word missing
    EXPECT(0x1000, 0x1002 + 0x20, 0x4eba, 0x0020);  // jsr d16(PC)
    EXPECT(0x1000, 0x1002 - 8, 0x4efa, 0xfff8);     // jmp d16(PC)
    EXPECT(0x1000, NONE, 0x4eb9, 0x00c0, 0x1234);   // jsr abs.l: address already shown
    EXPECT(0x1000, NONE, 0x4e75);                   // rts
    EXPECT(0x1000, NONE, 0x4e93);                   // jsr (a3)
    EXPECT(0x1000, NONE, 0x4e7a);                   // not a jump (movec)
    EXPECT(0x1000, NONE, 0x7001);                   // moveq
    EXPECT(0xfffffe, 0x000002, 0x6002);             // wraps on 24 bits
    EXPECT(0x000000, 0xfffffe, 0x60fc);             // wraps below 0
    printf("%d passed, %d failed\n", gnPassed, gnFailed);
    return gnFailed != 0;
}
