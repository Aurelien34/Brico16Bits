#include <cstring>

#include "CallStack.h"

/// @brief Work RAM of the Neo Geo (stack and A6 frames)
static constexpr uint32_t CALLSTACK_RAM_START = 0x100000;
static constexpr uint32_t CALLSTACK_RAM_END = 0x110000;   // Exclusive

/// @brief Zone where a return address may point to
struct CodeZone
{
    uint32_t start;   ///< Inclusive
    uint32_t end;     ///< Exclusive
    bool cacheable;   ///< Content never changes during a session (not banked, not RAM)
};

/// @brief Code zones. The vector table ($000000-$0000FF, swappable with the BIOS one) never holds code.
static const CodeZone gaCodeZones[] =
{
    { 0x000100, 0x100000, true },    // P-ROM, fixed bank
    { 0x200000, 0x300000, false },   // P-ROM, switchable bank
    { 0xC00000, 0xC20000, true },    // System ROM
};

static const CodeZone *FindCodeZone(uint32_t address)
{
    for (const CodeZone &zone : gaCodeZones)
    {
        if (address >= zone.start && address < zone.end)
        {
            return &zone;
        }
    }
    return nullptr;
}

CallStack::CallStack()
{
    mpMemory = nullptr;
    mnWindowBase = 0;
    mpWindow = nullptr;
    mnWindowWords = 0;
    mpCallers = nullptr;
    mnCallerCount = 0;
    mnMaxCallers = 0;
    mnLastReadCount = 0;
    Reset();
}

void CallStack::Reset()
{
    Invalidate();
    ClearCodeCache();
}

void CallStack::Invalidate()
{
    mnShadowCount = 0;
}

void CallStack::ClearCodeCache()
{
    mnCodeCacheCount = 0;
    mnCodeCacheNext = 0;
}

/// @brief Drop the innermost entries whose slot is below newSp (their frame has been released)
void CallStack::ReleaseFrames(uint32_t newSp)
{
    while (mnShadowCount > 0 && maShadow[mnShadowCount - 1].frameSp < newSp)
    {
        mnShadowCount--;
    }
}

/// @brief Push a new innermost frame. Entries at or below its slot are stale and dropped first,
/// so frameSp stays strictly decreasing from the oldest entry to the innermost one.
void CallStack::PushFrame(uint32_t returnAddress, uint32_t frameSp)
{
    ReleaseFrames(frameSp + 1);
    if (mnShadowCount == CALLSTACK_SHADOW_DEPTH)
    {
        // Keep the innermost frames : drop the oldest one
        memmove(&maShadow[0], &maShadow[1], sizeof(ShadowEntry) * (CALLSTACK_SHADOW_DEPTH - 1));
        mnShadowCount--;
    }
    maShadow[mnShadowCount].returnAddress = returnAddress;
    maShadow[mnShadowCount].frameSp = frameSp;
    mnShadowCount++;
}

void CallStack::OnStep(uint32_t prevPc, uint32_t prevSp, InstructionFlow flow, uint8_t size, uint32_t newPc, uint32_t newSp)
{
    if (flow == InstructionFlow::Call || flow == InstructionFlow::Trap)
    {
        uint32_t returnAddress = prevPc + size;
        uint32_t pushed = (flow == InstructionFlow::Call) ? 4 : 6;
        if (size == 0 || (size & 1) || (returnAddress & 1))
        {
            Invalidate();
        }
        else if (newSp == prevSp - pushed)
        {
            // Call : return address at newSp. Trap : SR at newSp, return address at newSp + 2.
            PushFrame(returnAddress, (flow == InstructionFlow::Call) ? newSp : newSp + 2);
        }
        else if (flow == InstructionFlow::Trap && newSp == prevSp && newPc == returnAddress)
        {
            // TRAPV not taken : nothing pushed
        }
        else
        {
            // Unexpected stack effect (stack switch, interrupt, wrong classification...)
            Invalidate();
        }
        return;
    }

    // Other, Loop, Return : any frame whose slot is now below SP has been released
    // (RTS / RTR / RTE pop it, addq.l #4,sp or move.l (sp)+,an discard it)
    ReleaseFrames(newSp);
}

void CallStack::OnRunUntilReached(uint32_t newSp)
{
    ReleaseFrames(newSp);
}

uint8_t CallStack::GetCallLength(const uint16_t *code, int wordCount)
{
    if (code == nullptr || wordCount <= 0)
    {
        return 0;
    }
    if (wordCount > 3)
    {
        code += wordCount - 3;
        wordCount = 3;
    }

    // 2 bytes : opcode at -2
    {
        uint16_t opcode = code[wordCount - 1];
        if ((opcode & 0xFFF8) == 0x4E90)
        {
            return 2;   // JSR (An)
        }
        if ((opcode & 0xFF00) == 0x6100 && (opcode & 0x00FF) != 0 && (opcode & 1) == 0)
        {
            return 2;   // BSR.S, even non zero displacement ($FF is odd : no BSR.L on the 68000)
        }
    }

    // 4 bytes : opcode at -4, one extension word
    if (wordCount >= 2)
    {
        uint16_t opcode = code[wordCount - 2];
        uint16_t extension = code[wordCount - 1];
        if ((opcode & 0xFFF8) == 0x4EA8)
        {
            return 4;   // JSR d16(An)
        }
        if ((opcode & 0xFFF8) == 0x4EB0 && (extension & 0x0700) == 0)
        {
            return 4;   // JSR d8(An,Xn), brief extension word (bits 8-10 are 0 on the 68000)
        }
        if (opcode == 0x4EB8 && (extension & 1) == 0)
        {
            return 4;   // JSR abs.W, even target
        }
        if (opcode == 0x4EBA && (extension & 1) == 0)
        {
            return 4;   // JSR d16(PC), even target
        }
        if (opcode == 0x4EBB && (extension & 0x0700) == 0)
        {
            return 4;   // JSR d8(PC,Xn)
        }
        if (opcode == 0x6100 && (extension & 1) == 0)
        {
            return 4;   // BSR.W, even displacement
        }
    }

    // 6 bytes : opcode at -6, two extension words
    if (wordCount >= 3)
    {
        if (code[0] == 0x4EB9 && code[1] <= 0x00FF && (code[2] & 1) == 0)
        {
            return 6;   // JSR abs.L, even 24 bits target
        }
    }
    return 0;
}

bool CallStack::IsInCodeZone(uint32_t address)
{
    if (address & 1)
    {
        return false;
    }
    const CodeZone *zone = FindCodeZone(address);
    return zone != nullptr && address >= zone->start + 2;
}

/// @brief Read words from the stack window if they are all inside it, else through the callback
bool CallStack::ReadWords(uint32_t address, uint16_t words, uint16_t *buffer)
{
    if (mpWindow != nullptr && address >= mnWindowBase
        && address + 2u * words <= mnWindowBase + 2u * mnWindowWords)
    {
        memcpy(buffer, &mpWindow[(address - mnWindowBase) >> 1], 2u * words);
        return true;
    }
    if (mpMemory == nullptr || mpMemory->read == nullptr)
    {
        return false;
    }
    mnLastReadCount++;
    return mpMemory->read(address, words, buffer, mpMemory->context);
}

/// @brief True if the address is a plausible return address : even, in a code zone, and preceded by
/// a valid JSR / BSR. The verdict of non banked code is cached across stops.
bool CallStack::IsReturnAddress(uint32_t address)
{
    if (!IsInCodeZone(address))
    {
        return false;
    }
    const CodeZone *zone = FindCodeZone(address);

    if (zone->cacheable)
    {
        for (int i = 0; i < mnCodeCacheCount; ++i)
        {
            if (maCodeCache[i].returnAddress == address)
            {
                return maCodeCache[i].valid;
            }
        }
    }

    // Up to 3 words before the address, without leaving the zone
    uint32_t start = address - 6;
    if (start < zone->start)
    {
        start = zone->start;
    }
    uint16_t wordCount = (uint16_t)((address - start) >> 1);
    uint16_t code[3];
    if (!ReadWords(start, wordCount, code))
    {
        return false;   // Not cached : the failure may be transient
    }
    bool valid = GetCallLength(code, wordCount) != 0;

    if (zone->cacheable)
    {
        maCodeCache[mnCodeCacheNext].returnAddress = address;
        maCodeCache[mnCodeCacheNext].valid = valid;
        mnCodeCacheNext = (mnCodeCacheNext + 1) % CALLSTACK_CODE_CACHE_SIZE;
        if (mnCodeCacheCount < CALLSTACK_CODE_CACHE_SIZE)
        {
            mnCodeCacheCount++;
        }
    }
    return valid;
}

/// @brief Append a caller. Returns false when the list is full.
bool CallStack::AddCaller(uint32_t returnAddress, uint32_t frameSp, CallerMethod method)
{
    if (mnCallerCount >= mnMaxCallers)
    {
        return false;
    }
    mpCallers[mnCallerCount].returnAddress = returnAddress;
    mpCallers[mnCallerCount].frameSp = frameSp;
    mpCallers[mnCallerCount].method = method;
    mnCallerCount++;
    return mnCallerCount < mnMaxCallers;
}

int CallStack::Analyze(uint32_t sp, uint32_t a6, const CallStackMemory &memory, Caller *callers, int maxCallers)
{
    mnLastReadCount = 0;
    mnCallerCount = 0;
    mpCallers = callers;
    mnMaxCallers = maxCallers < CALLSTACK_MAX_CALLERS ? maxCallers : CALLSTACK_MAX_CALLERS;
    if (callers == nullptr || mnMaxCallers <= 0)
    {
        return 0;
    }
    mpMemory = &memory;

    // Stack window
    mpWindow = nullptr;
    mnWindowBase = 0;
    mnWindowWords = 0;
    if (memory.stackWords != nullptr)
    {
        mpWindow = memory.stackWords;
        mnWindowBase = memory.stackBase;
        mnWindowWords = memory.stackWordCount;
    }
    else if (memory.read != nullptr && (sp & 1) == 0 && sp >= CALLSTACK_RAM_START && sp < CALLSTACK_RAM_END)
    {
        uint32_t words = (CALLSTACK_RAM_END - sp) >> 1;
        if (words > CALLSTACK_SCAN_MAX_WORDS)
        {
            words = CALLSTACK_SCAN_MAX_WORDS;
        }
        mnLastReadCount++;
        if (memory.read(sp, (uint16_t)words, maStackBuffer, memory.context))
        {
            mpWindow = maStackBuffer;
            mnWindowBase = sp;
            mnWindowWords = (uint16_t)words;
        }
    }

    // Slots of the next callers must be at or above this address
    uint32_t floor = sp;
    bool more = true;

    // 1. Shadow stack, innermost first
    for (int i = mnShadowCount - 1; i >= 0 && more; --i)
    {
        if (maShadow[i].frameSp < floor)
        {
            continue;
        }
        more = AddCaller(maShadow[i].returnAddress, maShadow[i].frameSp, CallerMethod::Shadow);
        floor = maShadow[i].frameSp + 4;
    }

    // 2. A6 chain. Links inside the frames already found by the shadow stack are followed but not reported.
    uint32_t frame = a6;
    uint32_t lowest = sp;
    for (int level = 0; level < CALLSTACK_A6_MAX_DEPTH && more; ++level)
    {
        if ((frame & 1) || frame < CALLSTACK_RAM_START || frame > CALLSTACK_RAM_END - 8 || frame < lowest)
        {
            break;
        }
        uint16_t words[4];
        if (!ReadWords(frame, 4, words))
        {
            break;
        }
        uint32_t next = ((uint32_t)words[0] << 16) | words[1];
        uint32_t returnAddress = ((uint32_t)words[2] << 16) | words[3];
        uint32_t slot = frame + 4;
        if (slot >= floor)
        {
            if (!IsReturnAddress(returnAddress))
            {
                break;
            }
            more = AddCaller(returnAddress, slot, CallerMethod::A6);
            floor = slot + 4;
        }
        lowest = frame + 8;   // Strictly increasing, frames cannot overlap
        frame = next;
    }

    // 3. Stack scan above the frames already found, at most CALLSTACK_SCAN_MAX_WORDS words from SP
    if (mpWindow != nullptr && more)
    {
        uint32_t end = mnWindowBase + 2u * mnWindowWords;
        uint32_t scanEnd = sp + 2u * CALLSTACK_SCAN_MAX_WORDS;
        if (end > scanEnd)
        {
            end = scanEnd;
        }
        uint32_t slot = floor > mnWindowBase ? floor : mnWindowBase;
        slot = (slot + 1) & ~1u;
        while (more && slot + 4 <= end)
        {
            const uint16_t *words = &mpWindow[(slot - mnWindowBase) >> 1];
            uint32_t value = ((uint32_t)words[0] << 16) | words[1];
            if (IsReturnAddress(value))
            {
                more = AddCaller(value, slot, CallerMethod::Scan);
                slot += 4;
            }
            else
            {
                slot += 2;
            }
        }
    }

    mpMemory = nullptr;
    mpWindow = nullptr;
    return mnCallerCount;
}
