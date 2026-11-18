// Host test for MemoryCursor / MemoryWriteMarks (not part of the Pico build)
#include <cstdio>
#include <cstring>
#include "MemoryCursor.h"

static int gChecks = 0, gFailures = 0;
#define CHECK(c) do { gChecks++; if (!(c)) { gFailures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void TestBasicMoves()
{
    MemoryCursor c;
    uint32_t zone = 0x1000;
    c.Reset(zone, true);
    CHECK(c.GetAddress() == 0x1000);
    CHECK(!c.MoveCells(1, &zone, 8));
    CHECK(c.GetAddress() == 0x1002);
    CHECK(!c.MoveCells(-1, &zone, 8));
    CHECK(c.GetAddress() == 0x1000);
    CHECK(!c.MoveLines(1, &zone, 8));
    CHECK(c.GetAddress() == 0x1010);
    CHECK(!c.MoveLines(-1, &zone, 8));
    CHECK(c.GetAddress() == 0x1000);

    // Byte mode
    c.SetWordMode(false);
    CHECK(!c.MoveCells(1, &zone, 8));
    CHECK(c.GetAddress() == 0x1001);
    // Back to words: aligned down
    c.SetWordMode(true);
    CHECK(c.GetAddress() == 0x1000);
    c.SetWordMode(false);
    c.MoveCells(3, &zone, 8);
    CHECK(c.GetAddress() == 0x1003);
    c.SetWordMode(true);
    CHECK(c.GetAddress() == 0x1002);
    CHECK(c.GetCellSize() == 2);
}

static void TestScrollAtEdges()
{
    MemoryCursor c;
    uint32_t zone = 0x1000;
    c.Reset(zone, true);
    // Down past the last line (8 lines: 0x1000..0x107f)
    c.MoveLines(7, &zone, 8);
    CHECK(c.GetAddress() == 0x1070 && zone == 0x1000);
    CHECK(c.MoveLines(1, &zone, 8));
    CHECK(c.GetAddress() == 0x1080 && zone == 0x1010);
    // Right past the end of the last line
    c.Reset(0x1000, true);
    zone = 0x1000;
    c.MoveLines(7, &zone, 8);
    c.MoveCells(7, &zone, 8);   // last cell of the last line: 0x107e
    CHECK(c.GetAddress() == 0x107e && zone == 0x1000);
    CHECK(c.MoveCells(1, &zone, 8));
    CHECK(c.GetAddress() == 0x1080 && zone == 0x1010);
    // Left before the first cell
    CHECK(c.MoveCells(-1, &zone, 8) == false);
    CHECK(c.GetAddress() == 0x107e && zone == 0x1010);
    c.Reset(0x1010, false);
    zone = 0x1010;
    CHECK(c.MoveCells(-1, &zone, 8));
    CHECK(c.GetAddress() == 0x100f && zone == 0x1000);
    // Up past the top
    c.Reset(0x2000, true);
    zone = 0x2000;
    CHECK(c.MoveLines(-1, &zone, 8));
    CHECK(c.GetAddress() == 0x1ff0 && zone == 0x1ff0);
}

static void TestAddressSpaceLimits()
{
    MemoryCursor c;
    uint32_t zone = 0;
    c.Reset(0, true);
    CHECK(!c.MoveCells(-1, &zone, 8));
    CHECK(c.GetAddress() == 0);
    CHECK(!c.MoveLines(-1, &zone, 8));
    CHECK(c.GetAddress() == 0);
    // End of the space: zone is clamped to 0xFFFF80
    zone = 0xfffff0;
    c.Reset(0xffff80, true);
    c.EnsureVisible(&zone, 8);
    CHECK(zone == 0xffff80);
    c.MoveLines(7, &zone, 8);
    CHECK(c.GetAddress() == 0xfffff0 && zone == 0xffff80);
    CHECK(!c.MoveLines(1, &zone, 8));
    CHECK(c.GetAddress() == 0xfffff0);
    c.MoveCells(100, &zone, 8);
    CHECK(c.GetAddress() == 0xfffffe);
    CHECK(zone == 0xffff80);
    c.SetWordMode(false);
    c.MoveCells(100, &zone, 8);
    CHECK(c.GetAddress() == 0xffffff);
    CHECK(!c.MoveLines(1, &zone, 8));
    CHECK(c.GetAddress() == 0xffffff);
    c.SetWordMode(true);
    CHECK(c.GetAddress() == 0xfffffe);
}

static void TestPages()
{
    MemoryCursor c;
    uint32_t zone = 0x1000;
    c.Reset(zone, true);
    c.MoveCells(5, &zone, 8);   // 0x100a
    CHECK(c.MovePage(1, &zone, 8));
    CHECK(zone == 0x1080 && c.GetAddress() == 0x108a);
    CHECK(c.MovePage(-1, &zone, 8));
    CHECK(zone == 0x1000 && c.GetAddress() == 0x100a);
    // Top of the space
    zone = 0x40;
    c.Reset(0x6c, true);
    CHECK(c.MovePage(-1, &zone, 8));
    CHECK(zone == 0 && c.GetAddress() == 0x2c);
    CHECK(!c.MovePage(-1, &zone, 8));
    CHECK(zone == 0 && c.GetAddress() == 0xc);
    // Bottom
    zone = 0xffff00;
    c.Reset(0xffff10, true);
    c.EnsureVisible(&zone, 8);
    CHECK(zone == 0xffff00);
    CHECK(c.MovePage(1, &zone, 8));
    CHECK(zone == 0xffff80 && c.GetAddress() == 0xffff90);
    CHECK(!c.MovePage(1, &zone, 8));
    CHECK(c.GetAddress() == 0xfffff0);
}

static void TestEnsureVisible()
{
    MemoryCursor c;
    uint32_t zone = 0x1000;
    c.Reset(0x1000, true);
    c.MoveLines(7, &zone, 8);       // 0x1070
    // Resize to 4 lines: cursor on the line 7 must stay visible
    CHECK(c.EnsureVisible(&zone, 4));
    CHECK(zone == 0x1040);
    CHECK(!c.EnsureVisible(&zone, 4));
    // Unaligned zone gets normalized
    zone = 0x1043;
    CHECK(c.EnsureVisible(&zone, 4));
    CHECK(zone == 0x1040);
    CHECK(!c.EnsureVisible(&zone, 0));
}

static void TestPending()
{
    MemoryCursor c;
    uint32_t zone = 0;
    char text[8];
    c.Reset(0, true);
    CHECK(!c.HasPending());
    CHECK(!c.AddDigit(0x1));
    CHECK(c.HasPending() && c.GetPendingDigits() == 1 && c.GetPendingValue() == 1);
    c.GetPendingText(text);
    CHECK(strcmp(text, "___1") == 0);
    CHECK(!c.AddDigit(0xf));
    c.GetPendingText(text);
    CHECK(strcmp(text, "__1f") == 0);
    CHECK(c.GetPendingValue() == 0x1f);
    CHECK(c.Backspace());
    CHECK(c.GetPendingValue() == 1 && c.GetPendingDigits() == 1);
    CHECK(c.Backspace());
    CHECK(!c.HasPending());
    CHECK(!c.Backspace());
    CHECK(!c.AddDigit(0xa));
    CHECK(!c.AddDigit(0xb));
    CHECK(!c.AddDigit(0xc));
    CHECK(c.AddDigit(0xd));
    CHECK(c.GetPendingValue() == 0xabcd);
    c.GetPendingText(text);
    CHECK(strcmp(text, "abcd") == 0);
    CHECK(!c.AddDigit(16));
    c.ClearPending();
    CHECK(!c.HasPending() && c.GetPendingValue() == 0);

    // Byte: complete after 2 digits
    c.SetWordMode(false);
    CHECK(!c.AddDigit(4));
    CHECK(c.AddDigit(2));
    CHECK(c.GetPendingValue() == 0x42);
    c.GetPendingText(text);
    CHECK(strcmp(text, "42") == 0);
    // A move drops the pending value
    c.ClearPending();
    c.AddDigit(7);
    c.MoveCells(1, &zone, 8);
    CHECK(!c.HasPending());
    // Toggling the size drops it too
    c.AddDigit(7);
    c.SetWordMode(true);
    CHECK(!c.HasPending());
}

static void TestMarks()
{
    MemoryWriteMarks m;
    CHECK(m.Lookup(0x100) == MemoryWriteMarks::Kind::None);
    m.Add(0x100, 2, false);
    CHECK(m.Lookup(0x100) == MemoryWriteMarks::Kind::Written);
    CHECK(m.Lookup(0x101) == MemoryWriteMarks::Kind::Written);
    CHECK(m.Lookup(0x102) == MemoryWriteMarks::Kind::None);
    m.Add(0x101, 1, true);
    CHECK(m.Lookup(0x101) == MemoryWriteMarks::Kind::Differs);
    CHECK(m.Lookup(0x100) == MemoryWriteMarks::Kind::Written);
    CHECK(m.GetCount() == 2);
    m.Clear();
    CHECK(m.GetCount() == 0 && m.Lookup(0x100) == MemoryWriteMarks::Kind::None);
    // Overflow drops the oldest
    for (int i = 0; i < MemoryWriteMarks::MAX_ENTRIES + 5; ++i)
    {
        m.Add(0x1000 + i, 1, false);
    }
    CHECK(m.GetCount() == MemoryWriteMarks::MAX_ENTRIES);
    CHECK(m.Lookup(0x1000) == MemoryWriteMarks::Kind::None);
    CHECK(m.Lookup(0x1000 + MemoryWriteMarks::MAX_ENTRIES + 4) == MemoryWriteMarks::Kind::Written);
}

int main()
{
    TestBasicMoves();
    TestScrollAtEdges();
    TestAddressSpaceLimits();
    TestPages();
    TestEnsureVisible();
    TestPending();
    TestMarks();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
