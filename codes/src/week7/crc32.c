#include "crc32.h"
/* Reflected IEEE CRC-32, same streaming API as Week 4. A 64-byte nibble
 * table trades a small ROM constant for two steps instead of eight/byte. */
uint32_t crc32_update(uint32_t state, const void *data, size_t size) {
    static const uint32_t table[16] = {
        0x00000000,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,
        0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c
    };
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        state ^= bytes[i];
        state = (state >> 4) ^ table[state & 15u];
        state = (state >> 4) ^ table[state & 15u];
    }
    return state;
}
uint32_t crc32(const void *data, size_t size) {
    return crc32_update(UINT32_MAX, data, size) ^ UINT32_MAX;
}
