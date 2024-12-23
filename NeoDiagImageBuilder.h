#pragma once

class NeoDiagImageBuilder
{
public:
    static bool CheckAndBuildImage();
private:
    static uint32_t crc32_for_byte(uint32_t r);
    static void crc32(const void *data, size_t n_bytes, uint32_t* crc);
};