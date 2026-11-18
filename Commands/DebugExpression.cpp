#include "DebugExpression.h"
#include <cstring>

namespace
{
    int HexDigit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    char Lower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }

    /// @brief Parse [$|0x]hex on [begin, end). Fails on empty, bad digit or > 32 bits.
    bool ParseNumber(const char *begin, const char *end, uint32_t *value)
    {
        if (begin < end && *begin == '$')
        {
            begin++;
        }
        else if (end - begin >= 2 && begin[0] == '0' && (begin[1] == 'x' || begin[1] == 'X'))
        {
            begin += 2;
        }
        if (begin >= end)
        {
            return false;
        }
        uint32_t result = 0;
        for (const char *p = begin; p < end; p++)
        {
            int digit = HexDigit(*p);
            if (digit < 0 || (result >> 28) != 0)
            {
                return false;
            }
            result = (result << 4) | (uint32_t)digit;
        }
        *value = result;
        return true;
    }

    /// @brief Check whether [begin, end) is a register name.
    /// @param found set to true if it is a register name
    /// @return false if it is a register name but its value is unavailable (no frame)
    bool ReadRegister(const char *begin, const char *end, const DebugFrame *frame, bool *found, uint32_t *value)
    {
        *found = false;
        if (end - begin != 2)
        {
            return true;
        }
        char c0 = Lower(begin[0]);
        char c1 = Lower(begin[1]);
        const uint32_t *source = nullptr;
        if ((c0 == 'd' || c0 == 'a') && c1 >= '0' && c1 <= '7')
        {
            *found = true;
            if (frame != nullptr)
            {
                source = (c0 == 'd') ? &frame->d[c1 - '0'] : &frame->a[c1 - '0'];
            }
        }
        else if (c0 == 's' && c1 == 'p')
        {
            *found = true;
            if (frame != nullptr)
            {
                source = &frame->a[7];
            }
        }
        else if (c0 == 'p' && c1 == 'c')
        {
            *found = true;
            if (frame != nullptr)
            {
                source = &frame->pc;
            }
        }
        if (source == nullptr)
        {
            return !*found;
        }
        *value = *source;
        return true;
    }
}

bool DebugExpression::Parse(const char *text, const DebugFrame *frame, uint32_t *value)
{
    if (text == nullptr || value == nullptr)
    {
        return false;
    }
    const char *begin = text;
    const char *end = text + strlen(text);
    while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    if (begin == end)
    {
        return false;
    }

    // Split at the first '+' or '-' (never part of a hex number)
    const char *op = begin;
    while (op < end && *op != '+' && *op != '-') op++;

    uint32_t baseValue = 0;
    bool found = false;
    if (!ReadRegister(begin, op, frame, &found, &baseValue))
    {
        return false;
    }
    if (!found && !ParseNumber(begin, op, &baseValue))
    {
        return false;
    }

    uint32_t result = baseValue;
    if (op < end)
    {
        uint32_t offset;
        if (!ParseNumber(op + 1, end, &offset))   // also rejects a second sign
        {
            return false;
        }
        if (*op == '+')
        {
            if (offset > 0xFFFFFFFFu - baseValue) return false;
            result = baseValue + offset;
        }
        else
        {
            if (offset > baseValue) return false;
            result = baseValue - offset;
        }
    }
    *value = result;
    return true;
}
