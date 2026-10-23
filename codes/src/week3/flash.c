#include "flash.h"

static const flash_partition partitions[FLASH_PARTITION_COUNT] = {
    {0x00000u, 0x10000u}, {0x10000u, 0x70000u},
    {0x80000u, 0x70000u}, {0xf0000u, 0x10000u}
};

const flash_partition *flash_partition_get(flash_partition_id id) {
    return (unsigned)id < FLASH_PARTITION_COUNT ? &partitions[id] : 0;
}

int flash_sim_bind(flash_device *dev, volatile uint8_t *memory, uint32_t size) {
    if (!dev || !memory || size != FLASH_SIZE) return FLASH_ARGUMENT;
    dev->memory = memory;
    return FLASH_OK;
}

static int check(const flash_device *dev, flash_partition_id id,
                 uint32_t offset, uint32_t size) {
    const flash_partition *part = flash_partition_get(id);
    if (!dev || !dev->memory || !part) return FLASH_ARGUMENT;
    /* Subtraction avoids overflow for untrusted offset/length values. */
    if (offset > part->size || size > part->size - offset) return FLASH_RANGE;
    return FLASH_OK;
}

static int aliases(const flash_device *dev, const void *ptr, uint32_t size) {
    uintptr_t p = (uintptr_t)ptr, base = (uintptr_t)dev->memory;
    /* Reject wrapping buffers and all aliases of this backend: reads must not
       become a way to bypass write protection; program input stays stable. */
    if (size > UINTPTR_MAX - p) return 1;
    return p < base + FLASH_SIZE && base < p + size;
}

int flash_read(const flash_device *dev, flash_partition_id id,
               uint32_t offset, void *output, uint32_t size) {
    int rc = check(dev, id, offset, size);
    if (rc) return rc;
    if (size == 0u) return FLASH_OK;
    if (!output || aliases(dev, output, size)) return FLASH_ARGUMENT;
    uint8_t *dst = output;
    uint32_t start = partitions[id].offset + offset;
    for (uint32_t i = 0; i < size; ++i) dst[i] = dev->memory[start + i];
    return FLASH_OK;
}

int flash_program(flash_device *dev, flash_partition_id id,
                  uint32_t offset, const void *input, uint32_t size) {
    int rc = check(dev, id, offset, size);
    if (rc) return rc;
    if (id == FLASH_BOOT) return FLASH_PROTECTED;
    if (!size || !input || aliases(dev, input, size)) return FLASH_ARGUMENT;
    if (offset % FLASH_PROGRAM_SIZE || size % FLASH_PROGRAM_SIZE)
        return FLASH_ALIGNMENT;
    const uint8_t *src = input;
    uint32_t start = partitions[id].offset + offset;
    /* Validate the entire request before changing a byte. This only guarantees
       no partial writes on validation errors, not atomicity under power loss. */
    for (uint32_t i = 0; i < size; ++i)
        if ((dev->memory[start + i] & src[i]) != src[i]) return FLASH_NEEDS_ERASE;
    for (uint32_t i = 0; i < size; ++i) dev->memory[start + i] &= src[i];
    return FLASH_OK;
}

int flash_erase(flash_device *dev, flash_partition_id id,
                uint32_t offset, uint32_t size) {
    int rc = check(dev, id, offset, size);
    if (rc) return rc;
    if (id == FLASH_BOOT) return FLASH_PROTECTED;
    if (!size) return FLASH_ARGUMENT;
    if (offset % FLASH_ERASE_SIZE || size % FLASH_ERASE_SIZE)
        return FLASH_ALIGNMENT;
    uint32_t start = partitions[id].offset + offset;
    for (uint32_t i = 0; i < size; ++i) dev->memory[start + i] = 0xffu;
    return FLASH_OK;
}
