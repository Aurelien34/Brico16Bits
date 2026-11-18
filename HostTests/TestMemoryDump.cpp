// Host test for DebugExpression and MemoryDump (not part of the Pico build)
#include <cstdio>
#include <cstring>
#include "DebugExpression.h"
#include "MemoryDump.h"

static int gChecks = 0, gFailures = 0;
#define CHECK(c) do { gChecks++; if (!(c)) { gFailures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static DebugFrame gFrame;

static bool Ok(const char *s, uint32_t expected, const DebugFrame *f = &gFrame)
{
    uint32_t v = 0xDEADBEEF;
    bool r = DebugExpression::Parse(s, f, &v);
    if (!r || v != expected) { printf("  expr '%s': ok=%d value=%08X (expected %08X)\n", s, r, (unsigned)v, (unsigned)expected); return false; }
    return true;
}
static bool Bad(const char *s, const DebugFrame *f = &gFrame)
{
    uint32_t v = 0x12345678;
    bool r = DebugExpression::Parse(s, f, &v);
    if (r || v != 0x12345678) { printf("  expr '%s' should be rejected\n", s); return false; }
    return true;
}

int main()
{
    memset(&gFrame, 0, sizeof gFrame);
    for (int i = 0; i < 8; i++) { gFrame.d[i] = 0x1000 + i; gFrame.a[i] = 0x2000 + i * 0x100; }
    gFrame.pc = 0xC00400;

    CHECK(Ok("c0a000", 0xC0A000));
    CHECK(Ok("$c0a000", 0xC0A000));
    CHECK(Ok("$C0A000", 0xC0A000));
    CHECK(Ok("0xc0a000", 0xC0A000));
    CHECK(Ok("0XC0A000", 0xC0A000));
    CHECK(Ok("0", 0));
    CHECK(Ok("ffffffff", 0xFFFFFFFF));
    CHECK(Ok("  10  ", 0x10));
    CHECK(Ok("add", 0xADD));            // not a register
    CHECK(Ok("d0", 0x1000));
    CHECK(Ok("D7", 0x1007));
    CHECK(Ok("a0", 0x2000));
    CHECK(Ok("A6", 0x2600));
    CHECK(Ok("a7", 0x2700));
    CHECK(Ok("sp", 0x2700));
    CHECK(Ok("SP", 0x2700));
    CHECK(Ok("pc", 0xC00400));
    CHECK(Ok("a6-$10", 0x25F0));
    CHECK(Ok("a6+$10", 0x2610));
    CHECK(Ok("a6+10", 0x2610));
    CHECK(Ok("a6-0x10", 0x25F0));
    CHECK(Ok("sp+4", 0x2704));
    CHECK(Ok("pc+2", 0xC00402));
    CHECK(Ok("c00000+$100", 0xC00100));
    CHECK(Ok("$c00000-0", 0xC00000));
    CHECK(Ok("ffffff00+ff", 0xFFFFFFFF));
    CHECK(Ok("ffffffff-ffffffff", 0));

    CHECK(Bad(""));
    CHECK(Bad("   "));
    CHECK(Bad("xyz"));
    CHECK(Bad("$"));
    CHECK(Bad("0x"));
    CHECK(Bad("12g"));
    CHECK(Bad("100000000"));            // > 32 bits
    CHECK(Bad("$1ffffffff"));
    CHECK(Ok("d8", 0xD8));          // bare hex, not a register
    CHECK(Ok("a8", 0xA8));
    CHECK(Ok("d", 0xD));
    CHECK(Ok("d00", 0xD00));
    CHECK(Bad("sp2"));
    CHECK(Bad("a6+"));
    CHECK(Bad("a6-"));
    CHECK(Bad("a6+-4"));
    CHECK(Bad("a6++4"));
    CHECK(Bad("+4"));
    CHECK(Bad("a6+4 5"));
    CHECK(Bad("a6+zz"));
    CHECK(Bad("a6+100000000"));
    CHECK(Bad("ffffffff+1"));           // overflow
    CHECK(Bad("10-11"));                // underflow
    CHECK(Bad("a6+4+4"));               // single offset only
    CHECK(Bad("a6 +4 x"));
    CHECK(Bad("d0", nullptr));          // registers need a frame
    CHECK(Bad("sp", nullptr));
    CHECK(Bad("pc+1", nullptr));
    CHECK(Ok("c0a000", 0xC0A000, nullptr));
    CHECK(Ok("c0a000+10", 0xC0A010, nullptr));
    { uint32_t v; CHECK(!DebugExpression::Parse(nullptr, &gFrame, &v)); CHECK(!DebugExpression::Parse("10", &gFrame, nullptr)); }

    // MemoryDump
    {
        MemoryDump d;
        CHECK(!d.HasCurrent() && !d.HasPrevious());
        CHECK(!d.IsByteChanged(0x1000));
        uint16_t w1[4] = { 0x1122, 0x3344, 0x5566, 0x7788 };
        d.PushWords(0x1000, w1, 4);
        CHECK(d.HasCurrent() && !d.HasPrevious());
        CHECK(d.Current().size == 8 && d.Current().address == 0x1000);
        uint8_t b;
        CHECK(d.GetByte(0x1000, &b) && b == 0x11);
        CHECK(d.GetByte(0x1001, &b) && b == 0x22);
        CHECK(d.GetByte(0x1007, &b) && b == 0x88);
        CHECK(!d.GetByte(0x1008, &b) && !d.GetByte(0xFFF, &b));
        CHECK(!d.IsByteChanged(0x1000));    // no previous

        uint16_t w2[4] = { 0x1122, 0x3355, 0x5566, 0x7799 };
        d.PushWords(0x1000, w2, 4);
        CHECK(d.HasPrevious());
        CHECK(!d.IsByteChanged(0x1000));
        CHECK(!d.IsByteChanged(0x1002));
        CHECK(d.IsByteChanged(0x1003));
        CHECK(!d.IsByteChanged(0x1006));
        CHECK(d.IsByteChanged(0x1007));
        CHECK(!d.IsByteChanged(0x1008));

        // Overlapping zones: only common addresses are compared
        uint16_t w3[4] = { 0x3355, 0xAAAA, 0xBBBB, 0xCCCC };   // at 0x1002
        d.PushWords(0x1002, w3, 4);
        CHECK(!d.IsByteChanged(0x1001));    // not in current
        CHECK(!d.IsByteChanged(0x1002));    // 0x33 == 0x33
        CHECK(!d.IsByteChanged(0x1003));    // 0x55 == 0x55
        CHECK(d.IsByteChanged(0x1004));     // 0xAA vs 0x55
        CHECK(d.IsByteChanged(0x1007));     // 0xCC vs 0x99
        CHECK(!d.IsByteChanged(0x1009));    // only in current
        CHECK(!d.IsByteChanged(0x100A));

        d.ClearPrevious();
        CHECK(!d.IsByteChanged(0x1004));
        d.Clear();
        CHECK(!d.HasCurrent());

        // High addresses, no wrap-around false positive
        uint8_t x[2] = { 1, 2 }, y[2] = { 3, 4 };
        d.Push(0xFFFFFFFE, x, 2);
        d.Push(0xFFFFFFFE, y, 2);
        CHECK(d.IsByteChanged(0xFFFFFFFE) && d.IsByteChanged(0xFFFFFFFF));
        CHECK(!d.IsByteChanged(0));
        d.Push(0x2000, x, 2);               // disjoint from previous
        CHECK(!d.IsByteChanged(0x2000));

        // Size clamping and SetCurrent
        static uint8_t big[1024];
        d.Clear();
        d.Push(0, big, 1024);
        CHECK(d.Current().size == MEMORY_DUMP_MAX_BYTES);
        static uint16_t bigw[300];
        d.PushWords(0, bigw, 300);
        CHECK(d.Current().size == MEMORY_DUMP_MAX_BYTES);
        d.Push(0, nullptr, 0);
        CHECK(!d.HasCurrent());
        d.Clear();
        d.Push(0x10, x, 2);
        d.SetCurrent(0x10, y, 2);           // no shift
        CHECK(!d.HasPrevious() && d.Current().bytes[0] == 3);
    }

    printf("%d checks, %d failures: %s\n", gChecks, gFailures, gFailures ? "FAIL" : "PASS");
    return gFailures ? 1 : 0;
}
