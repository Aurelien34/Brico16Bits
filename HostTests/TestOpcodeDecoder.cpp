// Host tests of OpcodeDecoder (68000 disassembler): text and number of words consumed.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "OpcodeDecoder.h"

static int gnFailures = 0;
static int gnChecks = 0;

// words: instruction words (followed by padding), expectedWords: words the decoder must consume
static void Check(std::initializer_list<uint16_t> words, const char *expectedText, int expectedWords)
{
    uint16_t buffer[16];
    memset(buffer, 0xAA, sizeof(buffer));
    int n = 0;
    for (uint16_t w : words) buffer[n++] = w;
    char text[128];
    int position = 0;
    OpcodeDecoder::DecodeInstruction(buffer, &position, text);
    ++gnChecks;
    if (strcmp(text, expectedText) != 0 || position != expectedWords)
    {
        ++gnFailures;
        printf("FAIL %04x : got \"%s\" (%d words), expected \"%s\" (%d words)\n",
               (unsigned)buffer[0], text, position, expectedText, expectedWords);
    }
}

int main()
{
    // MOVE to SR
    Check({0x46FC, 0x2700}, "move #$2700,sr", 2);
    Check({0x46C0}, "move d0,sr", 1);
    Check({0x46D3}, "move (a3),sr", 1);
    Check({0x46F9, 0x00C0, 0x1234}, "move ($c01234).l,sr", 3);
    Check({0x46C8}, "Unknown", 1); // An is not a valid source
    // MOVE to CCR (unchanged behaviour)
    Check({0x44C0}, "move d0,ccr", 1);
    Check({0x44FC, 0x0010}, "move #$10,ccr", 2);
    // MOVE from SR (unchanged)
    Check({0x40C1}, "move sr,d1", 1);

    // Logic immediate to SR
    Check({0x007C, 0x8000}, "ori #$8000,sr", 2);
    Check({0x027C, 0x7FFF}, "andi #$7fff,sr", 2);
    Check({0x0A7C, 0xA000}, "eori #$a000,sr", 2);
    // Logic immediate to CCR (same style as the SR variants)
    Check({0x003C, 0x0010}, "ori #$10,ccr", 2);
    Check({0x023C, 0x00EF}, "andi #$ef,ccr", 2);
    Check({0x0A3C, 0x0001}, "eori #$1,ccr", 2);
    Check({0x003C, 0x0100}, "Unknown", 1);  // CCR immediate above $FF is invalid

    // Static bit operations: bits 11-9 must be 100 (precedence bug fixed)
    Check({0x0800, 0x0003}, "btst #3,d0", 2);
    Check({0x0880, 0x0003}, "bclr #3,d0", 2);
    Check({0x08C0, 0x0003}, "bset #3,d0", 2);
    Check({0x0840, 0x0003}, "bchg #3,d0", 2);
    // Dynamic bit operations (unchanged)
    Check({0x0300}, "btst d1,d0", 1);
    // Other immediates that share the 0000 xxx0 pattern must not become bit operations
    Check({0x0400, 0x0012}, "subi.b #$12,d0", 2);
    Check({0x0600, 0x0012}, "addi.b #$12,d0", 2);
    Check({0x0C00, 0x0012}, "cmpi.b #$12,d0", 2);
    Check({0x0A00, 0x0012}, "eori.b #$12,d0", 2);

    // MOVE USP
    Check({0x4E60}, "move a0,usp", 1);
    Check({0x4E67}, "move a7,usp", 1);
    Check({0x4E68}, "move usp,a0", 1);
    Check({0x4E6B}, "move usp,a3", 1);

    // STOP / RESET / TRAPV
    Check({0x4E72, 0x2700}, "stop #$2700", 2);
    Check({0x4E70}, "reset", 1);
    Check({0x4E76}, "trapv", 1);

    // Regression on existing instructions
    Check({0x4E75}, "rts", 1);
    Check({0x4E73}, "rte", 1);
    Check({0x4E77}, "rtr", 1);
    Check({0x4E71}, "nop", 1);
    Check({0x4E41}, "trap #1", 1);
    Check({0x4E55, 0xFFF0}, "link a5,#$fff0", 2);
    Check({0x4E5D}, "unlk a5", 1);
    Check({0x4AFC}, "illegal", 1);
    Check({0x13C0, 0x0030, 0x0001}, "move.b d0,($300001).l", 3);
    Check({0x2700}, "move.l d0,-(a3)", 1);
    Check({0x0000, 0x0012}, "ori.b #$12,d0", 2);
    Check({0x0240, 0x00FF}, "andi.w #$ff,d0", 2);

    Check({0x6704}, "beq +4", 1);
    Check({0x6E71}, "bgt +113", 1); // used to be swallowed by the nop entry

    // NOP mask must be exact: these used to be swallowed by the nop entry
    Check({0x4E79}, "Unknown", 1);
    Check({0x4E7F}, "Unknown", 1);

    printf("%d checks, %d failures\n", gnChecks, gnFailures);
    printf(gnFailures ? "FAILED\n" : "PASSED\n");
    return gnFailures ? 1 : 0;
}
