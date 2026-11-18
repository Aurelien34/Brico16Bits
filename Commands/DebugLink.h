#pragma once

#include <cstdint>
#include "DebugProtocol.h"

/// @brief Stop frame received from the 68k handler (MAGIC already checked).
struct DebugStop
{
    uint16_t sr;   ///< Status register at the stop
    uint32_t pc;   ///< Program counter at the stop
    uint16_t ctr;  ///< Request counter sent by the 68k
};

/// @brief Registers returned by GET_REGS.
struct DebugRegisters
{
    uint32_t d[8];   ///< d0..d7
    uint32_t a[7];   ///< a0..a6
    uint32_t ssp;    ///< Supervisor stack pointer before the exception (exception frame address + 6)
    uint32_t usp;    ///< User stack pointer
    uint16_t sr;     ///< Status register
    uint32_t pc;     ///< Program counter

    /// @brief Effective a7 : SR.S ? SSP : USP
    uint32_t GetA7() const { return (sr & 0x2000) ? ssp : usp; }
};

/// @brief Last error of a DebugLink operation.
enum class DebugLinkError : uint8_t
{
    None = 0,
    Timeout,          ///< No response (EOT) before the timeout
    ShortResponse,    ///< EOT received with fewer words than expected
    InvalidArgument,  ///< Refused request (odd address, bad word count, forbidden zone, bad command...)
    Aborted,          ///< WaitStop abandoned because the poll callback returned false
    NotStopped,       ///< Request sent while no stop frame has been received yet
    LongResponse      ///< EOT received with more words than expected (desynchronisation)
};

/// @brief Callback called while waiting for a stop frame. Returning false abandons the wait.
typedef bool (*PollCallback)(void *context);

/// @brief Pico side of the debugger v2 protocol (no user interface).
/// Instance class: it holds state (request counter, timeout, error, statistics).
/// ResetMailbox() is static as it is used before any link exists (before the 68k reset).
/// Usage : ResetMailbox(), reset the 68k, then WaitStop() / GetRegisters() / ReadMemory() / Resume().
class DebugLink
{
public:
    DebugLink();

    /// @brief Reset the mailbox (ARG0, ARG1, BREAK = 0, CMD = Continue, then CTR = 0 last).
    /// To call before the 68k reset. A handler of a previous session still waiting for a request
    /// is woken up by CTR and leaves untraced, without sending a stop frame.
    static void ResetMailbox();

    /// @brief Wait for a stop frame. Stray words / EOT from the traced code are silently ignored.
    /// Rule : words are drained continuously into a sliding window of the last 5 words (kept across
    /// EOTs, cleared only on an accepted frame). When an EOT has been seen, the window is accepted if
    /// window[0] == DEBUG_PROTOCOL_MAGIC, window[4] == expected request counter and the PC is even.
    /// No blocking error: waits until a frame or abort.
    /// @param stop Receives the frame
    /// @param poll Called while waiting (may be nullptr); false abandons the wait (error Aborted)
    /// @param context Passed to poll
    bool WaitStop(DebugStop *stop, PollCallback poll = nullptr, void *context = nullptr);

    /// @brief GET_REGS request.
    bool GetRegisters(DebugRegisters *regs);

    /// @brief READ_MEM request. Refuses odd address, words == 0, words > 256 and, unless force is
    /// true, any range overlapping $300000-$3FFFFF or containing a word of $C00000-$CFFFFF whose
    /// (addr & 0x1FFFE) is 0x1FF00 / 0x1FF02 (BricoExt EOT / port out, watcher compares A1-A16 only).
    bool ReadMemory(uint32_t address, uint16_t words, uint16_t *buffer, bool force = false);

    /// @brief Resume execution : Step, Continue or RunUntil (arg0 = target PC, arg1 = minimum SP).
    /// Other commands set error InvalidArgument and send nothing. Then call WaitStop().
    void Resume(DebugCommand cmd, uint32_t arg0 = 0, uint32_t arg1 = 0);

    /// @brief Write BREAK = 1 (stop at the next passage in the handler). Does not touch CTR.
    void RequestBreak();

    /// @brief Timeout of the responses (GET_REGS, READ_MEM), in milliseconds. Default 1000.
    void SetResponseTimeoutMs(uint32_t ms) { mnResponseTimeoutMs = ms; }
    uint32_t GetResponseTimeoutMs() const { return mnResponseTimeoutMs; }

    DebugLinkError GetLastError() const { return meLastError; }
    static const char *GetErrorText(DebugLinkError error);

    /// @brief Number of frames with 5 words or more and no MAGIC received by WaitStop
    /// (e.g. v1 handler). Non blocking : the UI may display a message when it increases.
    uint32_t GetUnrecognizedFrameCount() const { return mnUnrecognizedFrames; }

    /// @brief Number of words received before the EOT of the last unrecognized frame.
    uint32_t GetLastUnrecognizedWordCount() const { return mnLastUnrecognizedWords; }

    /// @brief Words received beyond the expected count in the last response (discarded).
    uint32_t GetLastExtraWordCount() const { return mnLastExtraWords; }

    /// @brief Current request counter (value the 68k is waiting to see change).
    uint16_t GetCounter() const { return mnCounter; }

private:
    bool BeginRequest();
    void SendRequest(DebugCommand cmd, uint32_t arg0, uint32_t arg1);
    bool ReadResponse(uint16_t *words, uint32_t count);
    static void DiscardPendingInput();

    uint16_t mnCounter;
    bool mbCounterValid;
    uint32_t mnResponseTimeoutMs;
    DebugLinkError meLastError;
    uint32_t mnUnrecognizedFrames;
    uint32_t mnLastUnrecognizedWords;
    uint32_t mnLastExtraWords;
};
