#include <string.h>

#include "DebugFrame.h"
#include "OpcodeDecoder.h"

/// @brief Disassemble the code sent with a debug frame, starting at PC
/// @return Number of instructions written (stops at maxInstructions or at the end of the code sent by the 68k)
int DisassembleFrame(const DebugFrame *frame, DisassembledInstruction *instructions, int maxInstructions)
{
    // Copy the code with some zero padding: the decoder may read past the words sent by the 68k for the last instruction
    uint16_t code[DEBUG_FRAME_CODE_WORDS + 16] = {0};
    memcpy(code, frame->code, sizeof(frame->code));

    int count = 0;
    int position = 0;
    uint32_t address = frame->pc;
    while (count < maxInstructions && position < DEBUG_FRAME_CODE_WORDS)
    {
        DisassembledInstruction *instruction = &instructions[count++];
        int start = position;
        if (!OpcodeDecoder::DecodeInstruction(code, &position, instruction->text))
        {
            strcpy(instruction->text, "Unknown");
        }
        if (position <= start)
        {
            // Never get stuck on a decoder that did not consume anything
            position = start + 1;
        }

        instruction->address = address;
        instruction->truncated = position > DEBUG_FRAME_CODE_WORDS;
        instruction->wordCount = 0;
        for (int w = start; w < position && w < DEBUG_FRAME_CODE_WORDS && instruction->wordCount < DEBUG_INSTRUCTION_MAX_WORDS; ++w)
        {
            instruction->words[instruction->wordCount++] = code[w];
        }
        instruction->hasTarget = !instruction->truncated && strcmp(instruction->text, "Unknown") != 0 &&
            GetBranchTarget(instruction->address, instruction->words, instruction->wordCount, &instruction->target);
        if (!instruction->hasTarget)
        {
            instruction->target = 0;
        }
        address += (position - start) << 1;
    }
    return count;
}
