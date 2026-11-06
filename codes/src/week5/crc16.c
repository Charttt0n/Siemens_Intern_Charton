#include "crc16.h"

uint16_t crc16(const void *data, uint32_t size) {
    const uint8_t *bytes = data;
    uint16_t crc = 0;
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)((uint16_t)bytes[i] << 8);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}
