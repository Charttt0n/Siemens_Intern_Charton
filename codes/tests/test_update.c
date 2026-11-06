/* Host tests for the Week-5 update transaction (step 5).
 *
 * Covers metadata/target selection, RECEIVING-before-erase ordering, 4 byte
 * programming with arbitrary chunk sizes, READY validation, failure records,
 * abort, write/erase fault handling and an end-to-end YMODEM -> update run.
 * The active slot must never change here. */
#include "update.h"
#include "boot_meta.h"
#include "image.h"
#include "flash.h"
#include "crc32.h"
#include "ymodem.h"
#include "crc16.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

#define PACKAGE_MAX 0x70000u

static uint8_t memory[FLASH_SIZE];
static flash_device dev;

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static uint32_t slot_offset(flash_partition_id slot) {
    return flash_partition_get(slot)->offset;
}

static uint32_t slot_size(flash_partition_id slot) {
    return flash_partition_get(slot)->size;
}

static void reset_flash(void) {
    memset(memory, 0xFF, sizeof(memory));
    flash_sim_bind(&dev, memory, FLASH_SIZE);
}

/* Build a complete firmware file: image header + payload with valid vectors. */
static uint32_t build_package(uint8_t *out, uint32_t payload_size,
                              uint32_t version, int corrupt_payload) {
    uint8_t *payload = out + IMAGE_HEADER_SIZE;
    for (uint32_t i = 0; i < payload_size; ++i)
        payload[i] = (uint8_t)(i * 7u + version);
    put32(payload, 0x20020000u); /* MSP */
    for (unsigned i = 1; i < 48; ++i) {
        int reserved = (i >= 7 && i <= 10) || i == 13;
        put32(payload + 4u * i, reserved ? 0u : 0x100C1u);
    }
    uint32_t payload_crc = crc32(payload, payload_size);
    put32(out + 0, IMAGE_MAGIC);
    put32(out + 4, IMAGE_FORMAT);
    put32(out + 8, IMAGE_HEADER_SIZE);
    put32(out + 12, payload_size);
    put32(out + 16, APP_EXEC_BASE);
    put32(out + 20, version);
    put32(out + 24, payload_crc);
    put32(out + 28, crc32(out, 28));
    if (corrupt_payload) payload[payload_size / 2] ^= 0x01;
    return IMAGE_HEADER_SIZE + payload_size;
}

static int read_meta(meta_record *record) {
    meta_flash_ops ops;
    meta_flash_bind(&ops, &dev);
    return meta_read(&ops, record);
}

static int slot_erased(flash_partition_id slot) {
    uint32_t start = slot_offset(slot);
    for (uint32_t i = 0; i < slot_size(slot); ++i)
        if (memory[start + i] != 0xFFu) return 0;
    return 1;
}

static int slot_matches(flash_partition_id slot, const uint8_t *data,
                        uint32_t size) {
    uint32_t start = slot_offset(slot);
    for (uint32_t i = 0; i < size; ++i)
        if (memory[start + i] != data[i]) return 0;
    for (uint32_t i = size; i < slot_size(slot); ++i)
        if (memory[start + i] != 0xFFu) return 0;
    return 1;
}

static uint32_t slot_hash(flash_partition_id slot) {
    return crc32(memory + slot_offset(slot), slot_size(slot));
}

typedef struct {
    flash_device *dev;
    int fail_erase;
    int fail_program_at; /* 1-based program call */
    int programs;
    int meta_state_at_erase;
} fault_state;

static int fault_read(void *context, flash_partition_id slot, uint32_t offset,
                      void *out, uint32_t size) {
    return flash_read(((fault_state *)context)->dev, slot, offset, out, size);
}

static int fault_program(void *context, flash_partition_id slot, uint32_t offset,
                         const void *input, uint32_t size) {
    fault_state *fault = context;
    ++fault->programs;
    if (fault->fail_program_at && fault->programs == fault->fail_program_at)
        return -1;
    return flash_program(fault->dev, slot, offset, input, size);
}

static int fault_erase(void *context, flash_partition_id slot, uint32_t offset,
                       uint32_t size) {
    fault_state *fault = context;
    meta_flash_ops ops;
    meta_flash_bind(&ops, fault->dev);
    meta_record record;
    if (meta_read(&ops, &record) == META_OK)
        fault->meta_state_at_erase = record.state;
    if (fault->fail_erase) return -1;
    return flash_erase(fault->dev, slot, offset, size);
}

static void make_fault_ops(fault_state *fault, update_flash_ops *ops) {
    ops->context = fault;
    ops->read = fault_read;
    ops->program = fault_program;
    ops->erase = fault_erase;
}

static int test_success_update(void) {
    reset_flash();
    uint8_t old_package[PACKAGE_MAX], package[PACKAGE_MAX];
    uint32_t old_len = build_package(old_package, 333, 1, 0);
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t old_hash = slot_hash(FLASH_APP);
    uint32_t length = build_package(package, 333, 3, 0);

    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    CHECK(ctx.active == FLASH_APP && ctx.target == FLASH_BACKUP);
    meta_record record;
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_IDLE && record.active_slot == FLASH_APP);

    ymodem_sink sink;
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "fw.bin", length) == 0);
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_RECEIVING);
    CHECK(record.active_slot == FLASH_APP && record.target_slot == FLASH_BACKUP);
    CHECK(record.expected_length == 333 && record.progress == 0);
    CHECK(slot_erased(FLASH_BACKUP));

    /* Arbitrary chunk sizes exercise the 4 byte staging logic. */
    for (uint32_t offset = 0; offset < length; offset += 7) {
        uint32_t chunk = length - offset < 7 ? length - offset : 7;
        CHECK(sink.write(&ctx, package + offset, chunk) == 0);
    }
    CHECK(sink.finish(&ctx, length) == 0);
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_READY);
    CHECK(record.active_slot == FLASH_APP && record.target_slot == FLASH_BACKUP);
    CHECK(record.expected_length == 333 && record.progress == 333);
    CHECK(record.candidate_version == 3 && record.error == 0);
    CHECK(slot_hash(FLASH_APP) == old_hash);
    CHECK(slot_matches(FLASH_BACKUP, package, length));
    image_info info;
    CHECK(image_validate_slot(&dev, FLASH_BACKUP, &info) == IMAGE_OK);
    CHECK(info.version == 3 && info.size == 333);
    return 0;
}

static int test_bad_image_failure(void) {
    reset_flash();
    uint8_t old_package[PACKAGE_MAX], package[PACKAGE_MAX];
    uint32_t old_len = build_package(old_package, 333, 1, 0);
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t old_hash = slot_hash(FLASH_APP);
    uint32_t length = build_package(package, 333, 3, 1); /* payload CRC stale */

    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    ymodem_sink sink;
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "bad.bin", length) == 0);
    CHECK(sink.write(&ctx, package, length) == 0);
    CHECK(sink.finish(&ctx, length) == 1);
    meta_record record;
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_FAILED && record.error == UPDATE_IMAGE);
    CHECK(record.active_slot == FLASH_APP && record.target_slot == FLASH_BACKUP);
    CHECK(slot_hash(FLASH_APP) == old_hash);
    image_info info;
    CHECK(image_validate_slot(&dev, FLASH_BACKUP, &info) != IMAGE_OK);
    return 0;
}

static int test_erase_failure_keeps_receiving_order(void) {
    reset_flash();
    uint8_t old_package[PACKAGE_MAX], package[PACKAGE_MAX];
    uint32_t old_len = build_package(old_package, 333, 1, 0);
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t old_hash = slot_hash(FLASH_APP);
    uint32_t length = build_package(package, 333, 2, 0);

    fault_state fault;
    memset(&fault, 0, sizeof(fault));
    fault.dev = &dev;
    fault.fail_erase = 1;
    update_flash_ops ops;
    make_fault_ops(&fault, &ops);

    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, &ops, 0) == UPDATE_OK);
    ymodem_sink sink;
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "erase.bin", length) == 1);
    /* RECEIVING must have been persisted before the erase was attempted. */
    CHECK(fault.meta_state_at_erase == META_STATE_RECEIVING);
    meta_record record;
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_FAILED && record.error == UPDATE_ERASE);
    CHECK(record.active_slot == FLASH_APP);
    CHECK(slot_hash(FLASH_APP) == old_hash);
    return 0;
}

static int test_write_failure(void) {
    reset_flash();
    uint8_t old_package[PACKAGE_MAX], package[PACKAGE_MAX];
    uint32_t old_len = build_package(old_package, 333, 1, 0);
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t old_hash = slot_hash(FLASH_APP);
    uint32_t length = build_package(package, 333, 2, 0);

    fault_state fault;
    memset(&fault, 0, sizeof(fault));
    fault.dev = &dev;
    fault.fail_program_at = 2;
    update_flash_ops ops;
    make_fault_ops(&fault, &ops);

    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, &ops, 0) == UPDATE_OK);
    ymodem_sink sink;
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "write.bin", length) == 0);
    uint8_t chunk[16];
    memcpy(chunk, package, sizeof(chunk));
    CHECK(sink.write(&ctx, chunk, sizeof(chunk)) == 1);
    meta_record record;
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_FAILED && record.error == UPDATE_WRITE);
    CHECK(record.active_slot == FLASH_APP);
    CHECK(slot_hash(FLASH_APP) == old_hash);
    return 0;
}

static int test_abort_and_failure_record(void) {
    reset_flash();
    uint8_t old_package[PACKAGE_MAX], package[PACKAGE_MAX];
    uint32_t old_len = build_package(old_package, 333, 1, 0);
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t length = build_package(package, 333, 2, 0);

    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    ymodem_sink sink;
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "abort.bin", length) == 0);
    CHECK(sink.write(&ctx, package, 8) == 0);
    sink.abort(&ctx);
    meta_record record;
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_FAILED && record.error == UPDATE_ABORTED);
    CHECK(record.active_slot == FLASH_APP);
    CHECK(sink.finish(&ctx, length) == 1);
    return 0;
}

static int test_length_bounds(void) {
    reset_flash();
    update_context ctx;
    ymodem_sink sink;
    meta_record record;

    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "small.bin", IMAGE_HEADER_SIZE + 191u) == 1);
    CHECK(read_meta(&record) == META_OK && record.state == META_STATE_IDLE);
    CHECK(sink.begin(&ctx, "large.bin", APP_EXEC_SIZE + 1u) == 1);
    CHECK(read_meta(&record) == META_OK && record.state == META_STATE_IDLE);

    reset_flash();
    uint8_t package[PACKAGE_MAX];
    uint32_t length = build_package(package, 333, 2, 0);
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "mismatch.bin", length) == 0);
    CHECK(sink.write(&ctx, package, length) == 0);
    CHECK(sink.finish(&ctx, length - 1) == 1);
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_FAILED && record.error == UPDATE_LENGTH);
    CHECK(record.active_slot == FLASH_APP);
    return 0;
}

typedef struct {
    uint8_t stream[4096];
    uint32_t length;
    uint32_t position;
    uint32_t now;
} sender_script;

static int script_read(void *context, uint8_t *buffer, uint32_t size) {
    sender_script *script = context;
    (void)size;
    ++script->now;
    if (script->position < script->length) {
        buffer[0] = script->stream[script->position++];
        return 1;
    }
    return 0;
}

static int script_write(void *context, const uint8_t *data, uint32_t size) {
    (void)context; (void)data; (void)size;
    return 0;
}

static uint32_t script_time(void *context) {
    return ((sender_script *)context)->now;
}

static void script_frame(sender_script *script, uint8_t seq,
                         const uint8_t *data, uint32_t size) {
    uint8_t *p = script->stream + script->length;
    p[0] = size == 128u ? 0x01u : 0x02u;
    p[1] = seq;
    p[2] = (uint8_t)(0xFFu - seq);
    memcpy(p + 3, data, size);
    uint16_t crc = crc16(data, size);
    p[3 + size] = (uint8_t)(crc >> 8);
    p[4 + size] = (uint8_t)crc;
    script->length += 5u + size;
}

static void script_block0(sender_script *script, const char *name,
                          const char *length) {
    uint8_t data[128];
    memset(data, 0, sizeof(data));
    size_t n = strlen(name);
    memcpy(data, name, n);
    memcpy(data + n + 1, length, strlen(length));
    script_frame(script, 0, data, sizeof(data));
}

static void script_file(sender_script *script, const uint8_t *package,
                        uint32_t length) {
    char length_text[16];
    snprintf(length_text, sizeof(length_text), "%u", (unsigned)length);
    script_block0(script, "fw.bin", length_text);
    uint8_t data[128];
    uint8_t seq = 1;
    for (uint32_t offset = 0; offset < length; offset += 128) {
        uint32_t chunk = length - offset < 128 ? length - offset : 128;
        memset(data, 0x1A, sizeof(data));
        memcpy(data, package + offset, chunk);
        script_frame(script, seq++, data, sizeof(data));
    }
    script->stream[script->length++] = 0x04u; /* EOT */
    script_block0(script, "", "");
}

static int run_yodem_update(update_context *ctx, sender_script *script) {
    ymodem_io io;
    io.context = script;
    io.read = script_read;
    io.write = script_write;
    io.now_ms = script_time;
    ymodem_sink sink;
    update_sink_init(ctx, &sink);
    ymodem_config config;
    config.max_file_size = APP_EXEC_SIZE;
    config.timeout_ms = 10;
    config.retry_limit = 3;
    config.no_progress_timeout_ms = 0;
    ymodem_stats stats;
    return ymodem_receive(&io, &sink, &config, &stats);
}

static int test_yodem_end_to_end(void) {
    reset_flash();
    uint8_t old_package[PACKAGE_MAX], package[PACKAGE_MAX];
    uint32_t old_len = build_package(old_package, 333, 1, 0);
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t old_hash = slot_hash(FLASH_APP);
    uint32_t length = build_package(package, 333, 4, 0);

    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    sender_script script;
    memset(&script, 0, sizeof(script));
    script_file(&script, package, length);
    CHECK(run_yodem_update(&ctx, &script) == YMODEM_OK);
    meta_record record;
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_READY && record.candidate_version == 4);
    CHECK(record.active_slot == FLASH_APP);
    CHECK(slot_hash(FLASH_APP) == old_hash);
    CHECK(slot_matches(FLASH_BACKUP, package, length));

    /* Same path with a corrupt image: YMODEM transfer succeeds, update fails. */
    reset_flash();
    memcpy(memory + slot_offset(FLASH_APP), old_package, old_len);
    uint32_t bad_length = build_package(package, 333, 4, 1);
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    memset(&script, 0, sizeof(script));
    script_file(&script, package, bad_length);
    CHECK(run_yodem_update(&ctx, &script) == YMODEM_SINK);
    CHECK(read_meta(&record) == META_OK);
    CHECK(record.state == META_STATE_FAILED && record.error == UPDATE_IMAGE);
    CHECK(record.active_slot == FLASH_APP);
    CHECK(slot_hash(FLASH_APP) == old_hash);
    return 0;
}

static int test_argument_and_state_guards(void) {
    reset_flash();
    update_context ctx;
    CHECK(update_prepare(0, &dev, 0, 0) == UPDATE_ARGUMENT);
    CHECK(update_prepare(&ctx, 0, 0, 0) == UPDATE_ARGUMENT);
    ymodem_sink sink;
    update_sink_init(0, &sink); /* must not crash */
    CHECK(sink.context == 0);
    return 0;
}

static int test_activation(void) {
    reset_flash();
    uint8_t package[512];
    uint32_t length = build_package(package, 333, 2, 0);
    update_context ctx;
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    CHECK(update_activate(&ctx) == UPDATE_STATE);
    ymodem_sink sink;
    update_sink_init(&ctx, &sink);
    CHECK(sink.begin(&ctx, "fw.bin", length) == 0);
    CHECK(sink.write(&ctx, package, length) == 0);
    CHECK(sink.finish(&ctx, length) == 0);
    /* Candidate damage after READY must still block activation. */
    memory[slot_offset(FLASH_BACKUP)+300] ^= 1;
    CHECK(update_activate(&ctx) == UPDATE_IMAGE);
    meta_record current;
    CHECK(read_meta(&current) == META_OK && current.active_slot == FLASH_APP);
    memory[slot_offset(FLASH_BACKUP)+300] ^= 1;
    CHECK(update_activate(&ctx) == UPDATE_OK);
    CHECK(read_meta(&current) == META_OK);
    CHECK(current.active_slot == FLASH_BACKUP && current.state == META_STATE_IDLE);
    CHECK(current.target_slot == META_SLOT_NONE);
    /* A stale context cannot commit again, including after another writer. */
    CHECK(update_activate(&ctx) == UPDATE_STATE);
    CHECK(update_prepare(&ctx, &dev, 0, 0) == UPDATE_OK);
    CHECK(ctx.active == FLASH_BACKUP && ctx.target == FLASH_APP);
    update_sink_init(&ctx, &sink);
    length = build_package(package, 333, 3, 0);
    CHECK(sink.begin(&ctx, "fw3.bin", length) == 0);
    CHECK(sink.write(&ctx, package, length) == 0);
    CHECK(sink.finish(&ctx, length) == 0);
    CHECK(update_activate(&ctx) == UPDATE_OK);
    CHECK(read_meta(&current) == META_OK && current.active_slot == FLASH_APP);
    return 0;
}

int main(void) {
    static const struct { const char *name; int (*run)(void); } tests[] = {
        {"activation-and-reverse", test_activation},
        {"success-update", test_success_update},
        {"bad-image-failure", test_bad_image_failure},
        {"erase-failure-order", test_erase_failure_keeps_receiving_order},
        {"write-failure", test_write_failure},
        {"abort-failure-record", test_abort_and_failure_record},
        {"length-bounds", test_length_bounds},
        {"yodem-end-to-end", test_yodem_end_to_end},
        {"argument-guards", test_argument_and_state_guards},
    };
    for (unsigned i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        if (tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
    }
    puts("PASS: update metadata/erase/write/validate/failure and YMODEM end-to-end");
    return 0;
}
