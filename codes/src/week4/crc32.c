#include "crc32.h"
uint32_t crc32_update(uint32_t state, const void *data, size_t size) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        state ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ ((0u - (state & 1u)) & 0xedb88320u);
    }
    return state;
}
uint32_t crc32(const void *data, size_t size) {
    return crc32_update(UINT32_MAX, data, size) ^ UINT32_MAX;
}
