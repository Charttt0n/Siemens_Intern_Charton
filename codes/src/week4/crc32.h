#ifndef BOOT_CRC32_H
#define BOOT_CRC32_H
#include <stdint.h>
#include <stddef.h>
/* CRC-32/ISO-HDLC (zlib compatible): reflected 0xedb88320, init/xorout ~0.
   Update takes the internal state; begin with UINT32_MAX, finish with XOR ~0.
   data must address size bytes, or may be NULL when size is zero. */
uint32_t crc32_update(uint32_t state, const void *data, size_t size);
uint32_t crc32(const void *data, size_t size);
#endif
