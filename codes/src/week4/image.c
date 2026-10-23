#include "image.h"
#include "crc32.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int vectors_ok(const uint8_t *vectors, uint32_t size) {
    uint32_t msp = le32(vectors);
    if (msp <= APP_RAM_BASE || msp > APP_RAM_TOP || (msp & 7u)) return 0;
    for (unsigned i = 1; i < 48; ++i) {
        uint32_t entry = le32(vectors + 4u*i);
        int reserved = (i >= 7 && i <= 10) || i == 13;
        if (reserved) { if (entry) return 0; continue; }
        /* This format requires every implemented vector to have a handler. */
        if (!(entry & 1u) || (entry & ~1u) < APP_EXEC_BASE + APP_VECTOR_SIZE ||
            (entry & ~1u) >= APP_EXEC_BASE + size) return 0;
    }
    return 1;
}

int image_validate(const flash_device *dev, image_info *info) {
    uint8_t header[IMAGE_HEADER_SIZE], block[256];
    if (!info || flash_read(dev, FLASH_APP, 0, header, sizeof(header))) return IMAGE_IO;
    if (le32(header) != IMAGE_MAGIC || le32(header+4) != IMAGE_FORMAT ||
        le32(header+8) != IMAGE_HEADER_SIZE) return IMAGE_FORMAT_ERROR;
    if (crc32(header, 28) != le32(header+28)) return IMAGE_HEADER_CRC;
    uint32_t size = le32(header+12), load = le32(header+16);
    const flash_partition *part = flash_partition_get(FLASH_APP);
    if (size < APP_VECTOR_SIZE || size > part->size-IMAGE_HEADER_SIZE ||
        size > APP_EXEC_SIZE) return IMAGE_LENGTH;
    if (load != APP_EXEC_BASE) return IMAGE_ADDRESS;
    uint32_t state = UINT32_MAX;
    for (uint32_t offset = 0; offset < size;) {
        uint32_t count = size-offset < sizeof(block) ? size-offset : sizeof(block);
        if (flash_read(dev, FLASH_APP, IMAGE_HEADER_SIZE+offset, block, count)) return IMAGE_IO;
        state = crc32_update(state, block, count);
        offset += count;
    }
    if ((state ^ UINT32_MAX) != le32(header+24)) return IMAGE_PAYLOAD_CRC;
    if (flash_read(dev, FLASH_APP, IMAGE_HEADER_SIZE, block, APP_VECTOR_SIZE)) return IMAGE_IO;
    if (!vectors_ok(block, size)) return IMAGE_VECTOR;
    info->size = size; info->load = load; info->version = le32(header+20);
    info->crc = le32(header+24); info->msp = le32(block); info->reset = le32(block+4);
    return IMAGE_OK;
}

int image_load(const flash_device *dev, const image_info *info) {
    /* The caller uses only the result of image_validate. Repeat bounds before
       using a CPU destination address; no data from the header is a pointer. */
    if (!info || info->load != APP_EXEC_BASE || info->size < APP_VECTOR_SIZE ||
        info->size > APP_EXEC_SIZE-IMAGE_HEADER_SIZE) return IMAGE_LENGTH;
    return flash_read(dev, FLASH_APP, IMAGE_HEADER_SIZE, (void *)APP_EXEC_BASE,
                      info->size) ? IMAGE_IO : IMAGE_OK;
}

__attribute__((noinline)) int boot_verify_loaded(const image_info *info) {
    if (!info || info->load != APP_EXEC_BASE || info->size < APP_VECTOR_SIZE ||
        info->size > APP_EXEC_SIZE-IMAGE_HEADER_SIZE) return IMAGE_LENGTH;
    if (crc32((const void *)APP_EXEC_BASE, info->size) != info->crc) return IMAGE_COPY_CRC;
    if (!vectors_ok((const uint8_t *)APP_EXEC_BASE, info->size)) return IMAGE_VECTOR;
    return IMAGE_OK;
}
