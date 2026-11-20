/* Host sanitizer checks for the Week-6 trailer and RSA verify path. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "flash.h"
#include "secure_image.h"

static unsigned char nor[FLASH_SIZE];
extern int week6_host_write_slot(flash_device *dev, flash_partition_id slot,
                                 const uint8_t *data, uint32_t size);

int week6_host_write_slot(flash_device *dev, flash_partition_id slot,
                          const uint8_t *data, uint32_t size) {
    const flash_partition *part = flash_partition_get(slot);
    uint32_t offset;
    if (!part || size > part->size) return 1;
    if (flash_erase(dev, slot, 0, part->size)) return 1;
    for (offset = 0; offset + 4 <= size; offset += 4)
        if (flash_program(dev, slot, offset, data + offset, 4)) return 1;
    if (offset < size) {
        uint8_t pad[4] = {0xff, 0xff, 0xff, 0xff};
        memcpy(pad, data + offset, size - offset);
        if (flash_program(dev, slot, offset, pad, 4)) return 1;
    }
    return 0;
}

static int expect(int result, int wanted, const char *name) {
    if (result != wanted) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", name, result, wanted);
        return 1;
    }
    printf("PASS %s\n", name);
    return 0;
}

/* Images are supplied by the Python harness via a raw 1 MiB seed mapped
 * through stdin arguments: argv[1]=seed path is not used; packages are
 * pre-written by verify-week6 into a .bin the test opens. */
int main(int argc, char **argv) {
    flash_device dev = {0};
    image_info info;
    FILE *file;
    uint8_t slot_a[APP_EXEC_SIZE];
    uint8_t slot_b[APP_EXEC_SIZE];
    uint32_t size_a, size_b;
    int failed = 0;
    if (argc != 3) {
        fprintf(stderr, "usage: test_secure_image signed.pkg unsigned_or_bad.pkg\n");
        return 2;
    }
    memset(nor, 0xff, sizeof(nor));
    if (flash_sim_bind(&dev, nor, FLASH_SIZE) || secure_init() != SECURE_OK)
        return 1;
    file = fopen(argv[1], "rb");
    if (!file) return 1;
    size_a = (uint32_t)fread(slot_a, 1, sizeof(slot_a), file);
    fclose(file);
    file = fopen(argv[2], "rb");
    if (!file) return 1;
    size_b = (uint32_t)fread(slot_b, 1, sizeof(slot_b), file);
    fclose(file);
    if (week6_host_write_slot(&dev, FLASH_APP, slot_a, size_a)) return 1;
    failed |= expect(secure_validate_slot(&dev, FLASH_APP, &info), SECURE_OK, "signed-ok");
    if (week6_host_write_slot(&dev, FLASH_BACKUP, slot_b, size_b)) return 1;
    failed |= expect(secure_validate_slot(&dev, FLASH_BACKUP, &info) != SECURE_OK, 1, "bad-rejected");
    /* Flip one payload bit in the good slot and require failure. */
    nor[0x10000 + IMAGE_HEADER_SIZE + 64] ^= 1u;
    failed |= expect(secure_validate_slot(&dev, FLASH_APP, &info) != SECURE_OK, 1, "payload-flip");
    return failed ? 1 : 0;
}
