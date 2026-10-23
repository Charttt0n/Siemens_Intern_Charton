#include "flash.h"
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

/* Same assertions execute natively with sanitizers and on the Cortex-M7. */
int flash_selftest(volatile uint8_t *memory) {
    flash_device dev = {0};
    uint8_t data[8] = {0xf0, 0x55, 0xaa, 0x00, 0x11, 0x22, 0x33, 0x44};
    uint8_t output[8], ones[8] = {255,255,255,255,255,255,255,255};
    for (uint32_t i = 0; i < FLASH_SIZE; ++i) memory[i] = 0xff;
    CHECK(flash_sim_bind(0, memory, FLASH_SIZE) == FLASH_ARGUMENT);
    CHECK(flash_sim_bind(&dev, 0, FLASH_SIZE) == FLASH_ARGUMENT);
    CHECK(flash_sim_bind(&dev, memory, FLASH_SIZE-1) == FLASH_ARGUMENT);
    CHECK(flash_read(&dev, FLASH_APP, 0, output, 4) == FLASH_ARGUMENT);
    CHECK(flash_sim_bind(&dev, memory, FLASH_SIZE) == FLASH_OK);
    uint32_t end = 0;
    for (unsigned p = 0; p < FLASH_PARTITION_COUNT; ++p) {
        const flash_partition *part = flash_partition_get((flash_partition_id)p);
        CHECK(part && part->offset == end);
        CHECK(part->offset % FLASH_ERASE_SIZE == 0 && part->size % FLASH_ERASE_SIZE == 0);
        end += part->size;
    }
    CHECK(end == FLASH_SIZE);
    CHECK(!flash_partition_get((flash_partition_id)-1));
    CHECK(!flash_partition_get(FLASH_PARTITION_COUNT));
    CHECK(flash_read(&dev, FLASH_PARTITION_COUNT, 0, output, 4) == FLASH_ARGUMENT);
    CHECK(flash_read(0, FLASH_APP, 0, output, 4) == FLASH_ARGUMENT);
    CHECK(flash_read(&dev, FLASH_APP, 0, 0, 4) == FLASH_ARGUMENT);
    CHECK(flash_read(&dev, FLASH_APP, 0x70000, 0, 0) == FLASH_OK);
    CHECK(flash_read(&dev, FLASH_APP, 0x70000, output, 1) == FLASH_RANGE);
    CHECK(flash_read(&dev, FLASH_APP, UINT32_MAX, output, 4) == FLASH_RANGE);
    CHECK(flash_program(&dev, FLASH_APP, 4, data, UINT32_MAX) == FLASH_RANGE);
    CHECK(flash_erase(&dev, FLASH_APP, UINT32_MAX, FLASH_ERASE_SIZE) == FLASH_RANGE);
    CHECK(flash_program(&dev, FLASH_APP, 1, data, 4) == FLASH_ALIGNMENT);
    CHECK(flash_program(&dev, FLASH_APP, 0, data, 3) == FLASH_ALIGNMENT);
    CHECK(flash_program(&dev, FLASH_APP, 0, data, 0) == FLASH_ARGUMENT);
    CHECK(flash_program(&dev, FLASH_APP, 0, 0, 4) == FLASH_ARGUMENT);
    CHECK(flash_erase(&dev, FLASH_APP, 1, FLASH_ERASE_SIZE) == FLASH_ALIGNMENT);
    CHECK(flash_erase(&dev, FLASH_APP, 0, 4) == FLASH_ALIGNMENT);
    CHECK(flash_erase(&dev, FLASH_APP, 0, 0) == FLASH_ARGUMENT);
    CHECK(flash_program(&dev, FLASH_BOOT, 0, data, 4) == FLASH_PROTECTED);
    CHECK(flash_erase(&dev, FLASH_BOOT, 0, FLASH_ERASE_SIZE) == FLASH_PROTECTED);
    CHECK(flash_read(&dev, FLASH_APP, 0, (void *)memory, 4) == FLASH_ARGUMENT);
    CHECK(flash_program(&dev, FLASH_APP, 0, (const void *)memory, 4) == FLASH_ARGUMENT);
    for (unsigned p = FLASH_APP; p < FLASH_PARTITION_COUNT; ++p) {
        flash_partition_id id = (flash_partition_id)p;
        const flash_partition *part = flash_partition_get(id);
        CHECK(flash_program(&dev, id, part->size-4, data, 8) == FLASH_RANGE);
        CHECK(flash_erase(&dev, id, part->size-FLASH_ERASE_SIZE,
                          2*FLASH_ERASE_SIZE) == FLASH_RANGE);
        CHECK(flash_program(&dev, id, 0, data, 8) == FLASH_OK);
        CHECK(flash_program(&dev, id, 0, data, 8) == FLASH_OK);
        CHECK(flash_read(&dev, id, 0, output, 8) == FLASH_OK);
        for (unsigned i = 0; i < 8; ++i) CHECK(output[i] == data[i]);
        CHECK(flash_program(&dev, id, 0, ones, 8) == FLASH_NEEDS_ERASE);
        /* Last byte fails validation: the earlier valid byte must not change. */
        uint8_t mixed[8] = {0,0,0,0,0,0,0,255};
        CHECK(flash_program(&dev, id, 0, mixed, 8) == FLASH_NEEDS_ERASE);
        CHECK(flash_read(&dev, id, 0, output, 8) == FLASH_OK);
        for (unsigned i = 0; i < 8; ++i) CHECK(output[i] == data[i]);
        CHECK(flash_program(&dev, id, part->size-4, data, 4) == FLASH_OK);
        CHECK(flash_erase(&dev, id, 0, FLASH_ERASE_SIZE) == FLASH_OK);
        for (uint32_t i = 0; i < FLASH_ERASE_SIZE; ++i)
            CHECK(memory[part->offset+i] == 255);
        CHECK(memory[part->offset+part->size-4] == data[0]);
        CHECK(flash_erase(&dev, id, part->size-FLASH_ERASE_SIZE, FLASH_ERASE_SIZE) == FLASH_OK);
    }
    /* No out-of-partition mutation escaped; all bytes should be erased again. */
    for (uint32_t i = 0; i < FLASH_SIZE; ++i) CHECK(memory[i] == 255);
    return 0;
}
#ifdef FLASH_NATIVE_TEST
#include <stdio.h>
int main(void) {
    static uint8_t memory[FLASH_SIZE];
    int line = flash_selftest(memory);
    if (line) fprintf(stderr, "Flash assertion failed at line %d\n", line);
    else puts("PASS: flash API, partition boundaries, NOR rules, validation atomicity");
    return line ? 1 : 0;
}
#endif
