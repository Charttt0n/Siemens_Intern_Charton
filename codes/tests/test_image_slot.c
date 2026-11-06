/* Host-side tests for image_validate_slot()/image_load_slot() (Week-5 step 2).
 *
 * The successful copy path is exercised on the Cortex-M7 in verify-week5.py:
 * writing to APP_EXEC_BASE is not a legal host address. Everything that must
 * be rejected before any copy (slots, headers, lengths, CRCs, vectors) is
 * covered here, together with Week-4 compatibility of the old wrappers. */
#include "image.h"
#include "crc32.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return __LINE__; } } while (0)
#define PAYLOAD 512u
#define PAYLOAD_B 640u

static uint8_t memory[FLASH_SIZE];
static flash_device dev;

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static uint8_t *slot_base(flash_partition_id slot) {
    return memory + flash_partition_get(slot)->offset;
}
static void fill(uint8_t *p, uint32_t size, uint32_t seed) {
    for (uint32_t i = 0; i < size; ++i) p[i] = (uint8_t)(seed + i * 7u);
}
static void build_image(uint8_t *image, uint32_t size, uint32_t version) {
    fill(image + IMAGE_HEADER_SIZE, size, version);
    put32(image + IMAGE_HEADER_SIZE, 0x20020000u); /* MSP */
    for (unsigned i = 1; i < 48; ++i) {
        int reserved = (i >= 7 && i <= 10) || i == 13;
        put32(image + IMAGE_HEADER_SIZE + 4u*i, reserved ? 0u : 0x100C1u);
    }
    put32(image + 0, IMAGE_MAGIC);
    put32(image + 4, IMAGE_FORMAT);
    put32(image + 8, IMAGE_HEADER_SIZE);
    put32(image + 12, size);
    put32(image + 16, APP_EXEC_BASE);
    put32(image + 20, version);
    put32(image + 24, crc32(image + IMAGE_HEADER_SIZE, size));
    put32(image + 28, crc32(image, 28));
}
static void refresh_header(uint8_t *image) {
    put32(image + 28, crc32(image, 28));
}
static void refresh_payload(uint8_t *image, uint32_t size) {
    put32(image + 24, crc32(image + IMAGE_HEADER_SIZE, size));
    refresh_header(image);
}
static void clear_flash(void) {
    memset(memory, 0xff, sizeof(memory));
}

static int expect_validate(flash_partition_id slot, int expected) {
    image_info info;
    memset(&info, 0x5a, sizeof(info));
    int result = image_validate_slot(&dev, slot, &info);
    if (result != expected) {
        fprintf(stderr, "slot %d: got %d expected %d\n", (int)slot, result, expected);
        return __LINE__;
    }
    return 0;
}

int main(void) {
    image_info a = {0}, b = {0}, old = {0};

    if (flash_sim_bind(&dev, memory, FLASH_SIZE)) return 10;
    clear_flash();

    /* Both slots hold a legal image and report their own metadata. */
    build_image(slot_base(FLASH_APP), PAYLOAD, 1);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    CHECK(image_validate_slot(&dev, FLASH_APP, &a) == IMAGE_OK);
    CHECK(a.slot == FLASH_APP && a.size == PAYLOAD && a.version == 1);
    CHECK(a.load == APP_EXEC_BASE && a.msp == 0x20020000u && a.reset == 0x100C1u);
    CHECK(image_validate_slot(&dev, FLASH_BACKUP, &b) == IMAGE_OK);
    CHECK(b.slot == FLASH_BACKUP && b.size == PAYLOAD_B && b.version == 2);
    CHECK(b.msp == a.msp && b.reset == a.reset);
    CHECK(a.crc != b.crc);

    /* Week-4 wrappers still describe FLASH_APP only. */
    CHECK(image_validate(&dev, &old) == IMAGE_OK && old.slot == FLASH_APP &&
          old.version == 1);
    CHECK(image_load(&dev, &b) == IMAGE_SLOT);

    /* Partitions that cannot hold an application are refused by both APIs. */
    CHECK(image_slot_supported(FLASH_APP) && image_slot_supported(FLASH_BACKUP));
    CHECK(!image_slot_supported(FLASH_BOOT) && !image_slot_supported(FLASH_PARAMS));
    const flash_partition_id bad[] = {FLASH_BOOT, FLASH_PARAMS,
        (flash_partition_id)FLASH_PARTITION_COUNT, (flash_partition_id)-1};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        CHECK(image_validate_slot(&dev, bad[i], &b) == IMAGE_SLOT);
        CHECK(image_load_slot(&dev, bad[i], &a) == IMAGE_SLOT);
    }
    /* Metadata from one slot cannot be replayed on the other. */
    CHECK(image_load_slot(&dev, FLASH_BACKUP, &a) == IMAGE_SLOT);
    CHECK(image_load_slot(&dev, FLASH_APP, &b) == IMAGE_SLOT);
    CHECK(image_validate_slot(&dev, FLASH_APP, 0) == IMAGE_IO);
    CHECK(image_load_slot(&dev, FLASH_APP, 0) == IMAGE_LENGTH);

    /* Slot B uses the same rules as slot A: every malformation is refused. */
    clear_flash();
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + 0, 0);
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_FORMAT_ERROR) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + 20, 7); /* version changed, header CRC stale */
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_HEADER_CRC) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + 12, 0);
    refresh_header(slot_base(FLASH_BACKUP));
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_LENGTH) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + 12, 0xffffffffu);
    refresh_header(slot_base(FLASH_BACKUP));
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_LENGTH) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + 12, APP_EXEC_SIZE - IMAGE_HEADER_SIZE + 1u);
    refresh_header(slot_base(FLASH_BACKUP));
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_LENGTH) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + 16, 0x20000000u);
    refresh_header(slot_base(FLASH_BACKUP));
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_ADDRESS) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    slot_base(FLASH_BACKUP)[IMAGE_HEADER_SIZE + 300] ^= 0x01;
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_PAYLOAD_CRC) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + IMAGE_HEADER_SIZE + 4, 0x100C0u); /* even */
    refresh_payload(slot_base(FLASH_BACKUP), PAYLOAD_B);
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_VECTOR) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + IMAGE_HEADER_SIZE + 4 * 7, 0x100C1u);
    refresh_payload(slot_base(FLASH_BACKUP), PAYLOAD_B);
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_VECTOR) == 0);
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    put32(slot_base(FLASH_BACKUP) + IMAGE_HEADER_SIZE, 0x20010000u);
    refresh_payload(slot_base(FLASH_BACKUP), PAYLOAD_B);
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_VECTOR) == 0);

    /* An erased slot is not a plausible image, and A/B are independent. */
    clear_flash();
    build_image(slot_base(FLASH_BACKUP), PAYLOAD_B, 2);
    CHECK(image_validate(&dev, &old) == IMAGE_FORMAT_ERROR);
    CHECK(image_validate_slot(&dev, FLASH_BACKUP, &b) == IMAGE_OK);
    clear_flash();
    CHECK(expect_validate(FLASH_APP, IMAGE_FORMAT_ERROR) == 0);
    CHECK(expect_validate(FLASH_BACKUP, IMAGE_FORMAT_ERROR) == 0);

    puts("PASS: slot validation, illegal slots, bad CRC/length/vector, Week-4 wrappers");
    return 0;
}
