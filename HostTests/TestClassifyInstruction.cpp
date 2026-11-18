// Host tests of ClassifyInstruction (control flow category of an instruction, for step over / call stack).
#include <cstdio>
#include <cstdint>

#include "DebugFrame.h"

static int gnFailures = 0;
static int gnChecks = 0;

static const char *Name(InstructionFlow f)
{
    switch (f)
    {
    case InstructionFlow::Other: return "Other";
    case InstructionFlow::Call: return "Call";
    case InstructionFlow::Trap: return "Trap";
    case InstructionFlow::Loop: return "Loop";
    case InstructionFlow::Return: return "Return";
    }
    return "?";
}

static void Check(uint16_t op, InstructionFlow expected, const char *what)
{
    uint16_t words[2] = { op, 0x1234 };
    InstructionFlow got = ClassifyInstruction(words);
    ++gnChecks;
    if (got != expected)
    {
        ++gnFailures;
        printf("FAIL %04x (%s): expected %s, got %s\n", op, what, Name(expected), Name(got));
    }
}

int main()
{
    // JSR: all valid control modes
    Check(0x4e90, InstructionFlow::Call, "jsr (a0)");
    Check(0x4e97, InstructionFlow::Call, "jsr (a7)");
    Check(0x4ea8, InstructionFlow::Call, "jsr d16(a0)");
    Check(0x4eb0, InstructionFlow::Call, "jsr d8(a0,xn)");
    Check(0x4eb8, InstructionFlow::Call, "jsr abs.w");
    Check(0x4eb9, InstructionFlow::Call, "jsr abs.l");
    Check(0x4eba, InstructionFlow::Call, "jsr d16(pc)");
    Check(0x4ebb, InstructionFlow::Call, "jsr d8(pc,xn)");
    // JSR: invalid modes
    Check(0x4e80, InstructionFlow::Other, "jsr d0 invalid");
    Check(0x4e88, InstructionFlow::Other, "jsr a0 invalid");
    Check(0x4e98, InstructionFlow::Other, "jsr (a0)+ invalid");
    Check(0x4ea0, InstructionFlow::Other, "jsr -(a0) invalid");
    Check(0x4ebc, InstructionFlow::Other, "jsr #imm invalid");
    // JMP is Other, like every mode of it
    Check(0x4ed0, InstructionFlow::Other, "jmp (a0)");
    Check(0x4ef9, InstructionFlow::Other, "jmp abs.l");
    Check(0x4efa, InstructionFlow::Other, "jmp d16(pc)");

    // BSR
    Check(0x6100, InstructionFlow::Call, "bsr.w");
    Check(0x6102, InstructionFlow::Call, "bsr.s +2");
    Check(0x61fe, InstructionFlow::Call, "bsr.s -2");
    Check(0x61ff, InstructionFlow::Other, "bsr 32-bit displacement, invalid on 68000");
    // BRA and Bcc
    Check(0x6000, InstructionFlow::Other, "bra.w");
    Check(0x6004, InstructionFlow::Other, "bra.s");
    for (uint16_t cond = 2; cond < 16; ++cond)
    {
        Check((uint16_t)(0x6000 | (cond << 8) | 0x10), InstructionFlow::Other, "bcc.s");
        Check((uint16_t)(0x6000 | (cond << 8)), InstructionFlow::Other, "bcc.w");
    }

    // TRAP, TRAPV
    for (uint16_t n = 0; n < 16; ++n)
    {
        Check((uint16_t)(0x4e40 | n), InstructionFlow::Trap, "trap #n");
    }
    Check(0x4e76, InstructionFlow::Trap, "trapv");
    Check(0x4e50, InstructionFlow::Other, "link a0 (next after trap range)");
    Check(0x4e3f, InstructionFlow::Other, "before trap range");

    // DBcc: all 16 conditions
    for (uint16_t cond = 0; cond < 16; ++cond)
    {
        for (uint16_t reg = 0; reg < 8; ++reg)
        {
            Check((uint16_t)(0x50c8 | (cond << 8) | reg), InstructionFlow::Loop, "dbcc");
        }
    }
    Check(0x51c8, InstructionFlow::Loop, "dbra d0");
    // Scc / ADDQ / SUBQ share the high bits but are not DBcc
    Check(0x50c0, InstructionFlow::Other, "st d0");
    Check(0x51d0, InstructionFlow::Other, "sf (a0)");
    Check(0x5188, InstructionFlow::Other, "subq.l #8,a0");
    Check(0x5048, InstructionFlow::Other, "addq.w #8,a0");

    // Returns
    Check(0x4e75, InstructionFlow::Return, "rts");
    Check(0x4e77, InstructionFlow::Return, "rtr");
    Check(0x4e73, InstructionFlow::Return, "rte");
    Check(0x4e74, InstructionFlow::Other, "rtd does not exist on 68000");
    Check(0x4e72, InstructionFlow::Other, "stop");
    Check(0x4e70, InstructionFlow::Other, "reset");
    Check(0x4e71, InstructionFlow::Other, "nop");

    // Ordinary instructions
    Check(0x2200, InstructionFlow::Other, "move.l d0,d1");
    Check(0x46fc, InstructionFlow::Other, "move #imm,sr");
    Check(0x0000, InstructionFlow::Other, "ori.b");
    Check(0xffff, InstructionFlow::Other, "line F");
    Check(0x7001, InstructionFlow::Other, "moveq");
    Check(0x4850, InstructionFlow::Other, "pea (a0)");

    printf("%d checks, %d failures\n", gnChecks, gnFailures);
    if (gnFailures != 0)
    {
        return 1;
    }
    printf("OK\n");
    return 0;
}
