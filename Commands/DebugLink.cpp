#include "DebugLink.h"
#include "InputBuffer.h"
#include "ROMImageManager.h"
#include <pico/stdlib.h>
#include <hardware/sync.h>

#define DEFAULT_RESPONSE_TIMEOUT_MS 1000

/// @brief Forbidden zone of READ_MEM (reading it has side effects on the hardware)
#define READ_MEM_FORBIDDEN_START 0x300000u
#define READ_MEM_FORBIDDEN_END   0x3FFFFFu

/// @brief Highest address of the 68000 (24 bits address bus)
#define M68K_ADDRESS_MAX 0xFFFFFFu

/// @brief Bytes of the handler's stack below SSP_INTERRUPTED while it waits for a request:
/// exception frame (SR + PC = 6 bytes) and d0-d7/a0-a6 saved by the movem of Trace.s (15 * 4 bytes)
#define HANDLER_STACK_BYTES (6u + 15u * 4u)

/// @brief True if the word containing address is a BricoExt port (EOT / port out) or one of their
/// mirrors in $C00000-$CFFFFF: the watcher only compares A1-A16
static bool IsBricoExtPort(uint64_t address)
{
    if (address < 0xC00000u || address > 0xCFFFFFu)
    {
        return false;
    }
    uint32_t low = (uint32_t)address & 0x1FFFEu;
    return low == 0x1FF00u || low == 0x1FF02u;
}

/// @brief Store a 16 bits value in the mailbox. The image is read by the 68k as big-endian words;
/// the former byte-wise convention (low byte at the even offset) is a native little-endian 16 bits store.
static inline void WriteMailboxWord(uint32_t offset, uint16_t value)
{
    *(volatile uint16_t *)(ROMImageManager::GetImageBaseAddressB() + offset) = value;
}

/// @brief Store a 32 bits value: high word at offset, low word at offset + 2 (68k long layout).
static inline void WriteMailboxLong(uint32_t offset, uint32_t value)
{
    WriteMailboxWord(offset, (uint16_t)(value >> 16));
    WriteMailboxWord(offset + 2, (uint16_t)value);
}

DebugLink::DebugLink()
    : mnCounter(0), mbCounterValid(false), mnResponseTimeoutMs(DEFAULT_RESPONSE_TIMEOUT_MS),
      meLastError(DebugLinkError::None), mnUnrecognizedFrames(0), mnLastUnrecognizedWords(0), mnLastExtraWords(0),
      mnStopSsp(0), mbStopSspValid(false)
{
}

void DebugLink::ResetMailbox()
{
    WriteMailboxLong(DEBUG_MAILBOX_ARG0, 0);
    WriteMailboxLong(DEBUG_MAILBOX_ARG1, 0);
    WriteMailboxWord(DEBUG_MAILBOX_BREAK, 0);
    // CMD = Continue before CTR changes: an old handler woken up by CTR resumes untraced and sends no stop frame
    WriteMailboxWord(DEBUG_MAILBOX_CMD, (uint16_t)DebugCommand::Continue);
    __dmb();
    WriteMailboxWord(DEBUG_MAILBOX_CTR, 0);
    __dmb();
}

const char *DebugLink::GetErrorText(DebugLinkError error)
{
    switch (error)
    {
    case DebugLinkError::None: return "no error";
    case DebugLinkError::Timeout: return "timeout waiting for the 68k response";
    case DebugLinkError::ShortResponse: return "response too short";
    case DebugLinkError::InvalidArgument: return "invalid argument";
    case DebugLinkError::Aborted: return "wait aborted";
    case DebugLinkError::NotStopped: return "68k is not stopped in the handler";
    case DebugLinkError::LongResponse: return "response too long (desynchronised)";
    }
    return "unknown error";
}

void DebugLink::DiscardPendingInput()
{
    InputBuffer::CheckEOTSignalAndClearEOT();
    uint16_t tmp;
    while (InputBuffer::GetValue(&tmp)) { }
}

bool DebugLink::WaitStop(DebugStop *stop, PollCallback poll, void *context)
{
    meLastError = DebugLinkError::None;
    mbCounterValid = false;
    mbStopSspValid = false;

    // Sliding window of the last DEBUG_STOP_FRAME_WORDS words. It persists across EOTs (a stray EOT
    // may split the real frame in two) and is cleared only when a frame is accepted.
    uint16_t window[DEBUG_STOP_FRAME_WORDS];
    uint32_t windowTotal = 0;      // words pushed in the window since it was cleared
    uint32_t wordsSinceEot = 0;    // words received since the previous EOT evaluation

    while (true)
    {
        // 1. Sample the EOT first: the words of a frame reach the buffer before its EOT is raised,
        //    so everything sent before this EOT is available to the drain below (nothing is lost).
        bool bEOT = InputBuffer::CheckEOTSignalAndClearEOT();

        // 2. Drain continuously (also without EOT) : no overflow of the InputBuffer ring.
        uint16_t value;
        while (InputBuffer::GetValue(&value))
        {
            window[windowTotal % DEBUG_STOP_FRAME_WORDS] = value;
            ++windowTotal;
            ++wordsSinceEot;
        }

        // 3. Evaluate the window only when an EOT has been seen
        if (bEOT)
        {
            if (windowTotal >= DEBUG_STOP_FRAME_WORDS)
            {
                uint16_t frame[DEBUG_STOP_FRAME_WORDS];
                for (uint32_t i = 0; i < DEBUG_STOP_FRAME_WORDS; ++i)
                {
                    frame[i] = window[(windowTotal + i) % DEBUG_STOP_FRAME_WORDS];
                }
                uint32_t pc = ((uint32_t)frame[2] << 16) | frame[3];
                // The handler sends the CTR it read: 0 after ResetMailbox, then the last CTR written
                if (frame[0] == DEBUG_PROTOCOL_MAGIC && frame[4] == mnCounter && (pc & 1) == 0)
                {
                    stop->sr = frame[1];
                    stop->pc = pc;
                    stop->ctr = frame[4];
                    mbCounterValid = true;

                    // The frame EOT may not have been raised yet (stray EOT evaluated in the middle of
                    // the frame): wait for it (200 us max), then flush so that nothing of this frame
                    // pollutes the next response.
                    uint64_t deadline = time_us_64() + 200;
                    while (!InputBuffer::CheckEOTSignalAndClearEOT() && time_us_64() < deadline) { }
                    while (InputBuffer::GetValue(&value)) { }
                    return true;
                }
                if (wordsSinceEot >= DEBUG_STOP_FRAME_WORDS)
                {
                    ++mnUnrecognizedFrames;
                    mnLastUnrecognizedWords = wordsSinceEot;
                }
            }
            // else: EOT of the traced code, ignored
            wordsSinceEot = 0;
        }
        if (poll != nullptr && !poll(context))
        {
            meLastError = DebugLinkError::Aborted;
            return false;
        }
    }
}

bool DebugLink::BeginRequest()
{
    meLastError = DebugLinkError::None;
    if (!mbCounterValid)
    {
        meLastError = DebugLinkError::NotStopped;
        return false;
    }
    return true;
}

void DebugLink::SendRequest(DebugCommand cmd, uint32_t arg0, uint32_t arg1)
{
    // The 68k is blocked in the handler (CTR unchanged), so nothing can pollute the input now
    DiscardPendingInput();

    // Order matters: CTR last, after the data are visible
    WriteMailboxLong(DEBUG_MAILBOX_ARG0, arg0);
    WriteMailboxLong(DEBUG_MAILBOX_ARG1, arg1);
    WriteMailboxWord(DEBUG_MAILBOX_BREAK, 0);
    WriteMailboxWord(DEBUG_MAILBOX_CMD, (uint16_t)cmd);
    __dmb();
    ++mnCounter;
    WriteMailboxWord(DEBUG_MAILBOX_CTR, mnCounter);
    __dmb();
}

bool DebugLink::ReadResponse(uint16_t *words, uint32_t count)
{
    mnLastExtraWords = 0;
    uint64_t deadline = time_us_64() + (uint64_t)mnResponseTimeoutMs * 1000;
    while (!InputBuffer::CheckEOTSignalAndClearEOT())
    {
        if (time_us_64() >= deadline)
        {
            meLastError = DebugLinkError::Timeout;
            return false;
        }
    }
    // The EOT comes after the words: everything is already in the buffer
    uint32_t received = 0;
    uint16_t value;
    while (InputBuffer::GetValue(&value))
    {
        if (received < count)
        {
            words[received] = value;
        }
        else
        {
            ++mnLastExtraWords;
        }
        ++received;
    }
    if (received < count)
    {
        meLastError = DebugLinkError::ShortResponse;
        return false;
    }
    if (received > count)
    {
        meLastError = DebugLinkError::LongResponse;
        return false;
    }
    return true;
}

bool DebugLink::GetRegisters(DebugRegisters *regs)
{
    if (!BeginRequest())
    {
        return false;
    }
    SendRequest(DebugCommand::GetRegs, 0, 0);
    uint16_t raw[DEBUG_REGS_RESPONSE_WORDS];
    if (!ReadResponse(raw, DEBUG_REGS_RESPONSE_WORDS))
    {
        return false;
    }
    auto toLong = [&raw](int index) { return ((uint32_t)raw[index] << 16) | raw[index + 1]; };
    for (int i = 0; i < 8; ++i)
    {
        regs->d[i] = toLong(i * 2);
    }
    for (int i = 0; i < 7; ++i)
    {
        regs->a[i] = toLong(16 + i * 2);
    }
    regs->ssp = toLong(30);
    regs->usp = toLong(32);
    regs->sr = raw[34];
    regs->pc = toLong(35);
    mnStopSsp = regs->ssp;
    mbStopSspValid = true;
    return true;
}

bool DebugLink::ReadMemory(uint32_t address, uint16_t words, uint16_t *buffer, bool force)
{
    meLastError = DebugLinkError::None;
    if ((address & 1) || words == 0 || words > DEBUG_READ_MEM_MAX_WORDS || buffer == nullptr)
    {
        meLastError = DebugLinkError::InvalidArgument;
        return false;
    }
    uint64_t last = (uint64_t)address + (uint64_t)words * 2 - 1;
    if (last > 0xFFFFFFFFull ||
        (!force && address <= READ_MEM_FORBIDDEN_END && last >= READ_MEM_FORBIDDEN_START))
    {
        meLastError = DebugLinkError::InvalidArgument;
        return false;
    }
    if (!force)
    {
        // BricoExt ports: the watcher only compares A1-A16, so these words (and their mirrors in
        // $C00000-$CFFFFF) trigger an EOT / a port capture when read
        for (uint64_t a = address; a <= last; a += 2)
        {
            if (IsBricoExtPort(a))
            {
                meLastError = DebugLinkError::InvalidArgument;
                return false;
            }
        }
    }
    if (!BeginRequest())
    {
        return false;
    }
    SendRequest(DebugCommand::ReadMem, address, words);
    return ReadResponse(buffer, words);
}

bool DebugLink::WriteMemory(uint32_t address, uint16_t value, bool word, bool force)
{
    meLastError = DebugLinkError::None;
    uint32_t last = address + (word ? 1u : 0u);
    if (address > M68K_ADDRESS_MAX || (word && (address & 1)) || IsBricoExtPort(address) ||
        (!force && address <= READ_MEM_FORBIDDEN_END && last >= READ_MEM_FORBIDDEN_START))
    {
        meLastError = DebugLinkError::InvalidArgument;
        return false;
    }
    if (!BeginRequest())
    {
        return false;
    }
    if (!mbStopSspValid)
    {
        DebugRegisters regs;
        if (!GetRegisters(&regs))
        {
            return false;
        }
    }
    // Handler's stack: [SSP - HANDLER_STACK_BYTES, SSP), in the 24 bits address space
    int64_t stackEnd = (int64_t)(mnStopSsp & M68K_ADDRESS_MAX);
    int64_t stackStart = stackEnd - (int64_t)HANDLER_STACK_BYTES;
    if ((int64_t)last >= stackStart && (int64_t)address < stackEnd)
    {
        meLastError = DebugLinkError::InvalidArgument;
        return false;
    }
    SendRequest(DebugCommand::WriteMem, address, (uint32_t)value | (word ? DEBUG_WRITE_MEM_WORD : 0u));
    // Answer: EOT only
    return ReadResponse(nullptr, 0);
}

void DebugLink::Resume(DebugCommand cmd, uint32_t arg0, uint32_t arg1)
{
    if (cmd != DebugCommand::Step && cmd != DebugCommand::Continue && cmd != DebugCommand::RunUntil)
    {
        meLastError = DebugLinkError::InvalidArgument;
        return;
    }
    if (!BeginRequest())
    {
        return;
    }
    SendRequest(cmd, arg0, arg1);
    // The 68k leaves the handler: a new WaitStop is needed before any other request
    mbCounterValid = false;
    mbStopSspValid = false;
}

void DebugLink::RequestBreak()
{
    WriteMailboxWord(DEBUG_MAILBOX_BREAK, 1);
    __dmb();
}
