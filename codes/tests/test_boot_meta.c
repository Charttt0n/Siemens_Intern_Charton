/* Host tests for the Week-5 boot_meta transaction (steps 3).
 *
 * Production code is always boot_meta_init/read/commit over meta_flash_ops;
 * the fault adapter below only wraps the real Week-3 flash API so erase,
 * program and read failures and torn writes can be injected. Nothing here
 * reimplements the record state machine. */
#include "boot_meta.h"
#include "image.h"
#include "crc32.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

/* The metadata payload bounds must follow the firmware image format. */
typedef char meta_min_payload_matches_image[
    (META_MIN_PAYLOAD == APP_VECTOR_SIZE) ? 1 : -1];
typedef char meta_max_payload_matches_image[
    (META_MAX_PAYLOAD == APP_EXEC_SIZE - IMAGE_HEADER_SIZE) ? 1 : -1];

#define PARAMS_SIZE 0x10000u
#define FIRMWARE_SIZE 0xf0000u

typedef struct {
    flash_device dev;
    uint8_t *memory;
    unsigned reads, programs, erases;
    long fail_read_at;
    long fail_program_at;
    long fail_erase_at;
    long noop_program_at;
    uint32_t program_prefix;
    uint32_t erase_prefix;
    unsigned erase_offsets[64];
    unsigned erase_count;
    int range_error;
} fault_ctx;

static uint8_t memory[FLASH_SIZE];
static fault_ctx fx;
static meta_flash_ops ops;
static uint32_t firmware_crc;

static uint32_t params_offset(void) {
    return flash_partition_get(FLASH_PARAMS)->offset;
}

static uint32_t params_size(void) {
    return flash_partition_get(FLASH_PARAMS)->size;
}

static int params_range_ok(uint32_t offset, uint32_t size) {
    uint32_t limit = params_size();
    return offset <= limit && size <= limit - offset;
}

static int f_read(void *context, uint32_t offset, void *output, uint32_t size) {
    fault_ctx *fault = context;
    ++fault->reads;
    if (!params_range_ok(offset, size)) { fault->range_error = 1; return -1; }
    if (fault->fail_read_at && (long)fault->reads == fault->fail_read_at)
        return -1;
    return flash_read(&fault->dev, FLASH_PARAMS, offset, output, size);
}

static int f_program(void *context, uint32_t offset, const void *input,
                     uint32_t size) {
    fault_ctx *fault = context;
    ++fault->programs;
    if (!params_range_ok(offset, size) || offset % FLASH_PROGRAM_SIZE ||
        size % FLASH_PROGRAM_SIZE) {
        fault->range_error = 1;
        return -1;
    }
    if (fault->fail_program_at && (long)fault->programs == fault->fail_program_at) {
        /* Model a power loss in the middle of the write: an arbitrary byte
           prefix may land. NOR programming can only clear bits, so AND the
           prefix into the erased page directly; the 4-byte API rule is
           deliberately bypassed here because real hardware tears are not
           word aligned. */
        uint32_t prefix = fault->program_prefix;
        if (prefix > size) prefix = size;
        uint32_t base = params_offset() + offset;
        const uint8_t *src = input;
        for (uint32_t i = 0; i < prefix; ++i)
            fault->memory[base + i] &= src[i];
        return -1;
    }
    if (fault->noop_program_at && (long)fault->programs == fault->noop_program_at)
        return 0;
    return flash_program(&fault->dev, FLASH_PARAMS, offset, input, size);
}

static int f_erase(void *context, uint32_t offset, uint32_t size) {
    fault_ctx *fault = context;
    ++fault->erases;
    if (!params_range_ok(offset, size) || offset % FLASH_ERASE_SIZE ||
        size % FLASH_ERASE_SIZE) {
        fault->range_error = 1;
        return -1;
    }
    if (fault->erase_count < 64) fault->erase_offsets[fault->erase_count++] = offset;
    if (fault->fail_erase_at && (long)fault->erases == fault->fail_erase_at) {
        uint32_t prefix = fault->erase_prefix;
        if (prefix > size) prefix = size;
        uint32_t base = params_offset() + offset;
        for (uint32_t i = 0; i < prefix; ++i) fault->memory[base + i] = 0xFF;
        return -1;
    }
    return flash_erase(&fault->dev, FLASH_PARAMS, offset, size);
}

static void reset_all(void) {
    memset(memory, 0xFF, sizeof(memory));
    for (uint32_t i = 0; i < FIRMWARE_SIZE; ++i)
        memory[i] = (uint8_t)(i * 37u + 11u);
    memset(&fx, 0, sizeof(fx));
    fx.memory = memory;
    flash_sim_bind(&fx.dev, memory, FLASH_SIZE);
    ops.context = &fx;
    ops.read = f_read;
    ops.program = f_program;
    ops.erase = f_erase;
    firmware_crc = crc32(memory, FIRMWARE_SIZE);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int all_ff(const uint8_t *bytes, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
        if (bytes[i] != 0xFFu) return 0;
    return 1;
}

static int same_record(const meta_record *a, const meta_record *b) {
    return a->magic == b->magic && a->schema == b->schema &&
           a->generation == b->generation &&
           a->active_slot == b->active_slot &&
           a->target_slot == b->target_slot && a->state == b->state &&
           a->progress == b->progress &&
           a->expected_length == b->expected_length &&
           a->candidate_version == b->candidate_version &&
           a->error == b->error && a->crc == b->crc && a->marker == b->marker;
}

static void idle_record(meta_record *record, uint32_t generation) {
    memset(record, 0, sizeof(*record));
    record->generation = generation;
    record->active_slot = FLASH_APP;
    record->target_slot = META_SLOT_NONE;
    record->state = META_STATE_IDLE;
}

static void upgrade_record(meta_record *record, uint32_t state, uint32_t progress,
                           uint32_t expected, uint32_t version, uint32_t error) {
    memset(record, 0, sizeof(*record));
    record->active_slot = FLASH_APP;
    record->target_slot = FLASH_BACKUP;
    record->state = state;
    record->progress = progress;
    record->expected_length = expected;
    record->candidate_version = version;
    record->error = error;
}

/* Direct setup helper: production serializer + the real flash API, no faults. */
static int write_page_record(unsigned page, const meta_record *record) {
    uint8_t bytes[META_RECORD_SIZE];
    meta_encode(record, bytes);
    if (flash_erase(&fx.dev, FLASH_PARAMS, page * META_PAGE_SIZE, META_PAGE_SIZE))
        return 1;
    return flash_program(&fx.dev, FLASH_PARAMS, page * META_PAGE_SIZE, bytes,
                         sizeof(bytes)) != FLASH_OK;
}

static int page_record(unsigned page, uint8_t out[META_RECORD_SIZE]) {
    return flash_read(&fx.dev, FLASH_PARAMS, page * META_PAGE_SIZE, out,
                      META_RECORD_SIZE);
}

static int test_blank_then_init(void) {
    reset_all();
    meta_record record;
    CHECK(meta_read(&ops, &record) == META_NO_RECORD);
    CHECK(fx.reads == 2 && fx.programs == 0 && fx.erases == 0);
    memset(&record, 0xAA, sizeof(record));
    CHECK(meta_init(&ops, &record) == META_OK);
    CHECK(record.magic == META_MAGIC && record.schema == META_SCHEMA);
    CHECK(record.generation == 1 && record.active_slot == FLASH_APP);
    CHECK(record.target_slot == META_SLOT_NONE && record.state == META_STATE_IDLE);
    CHECK(!record.progress && !record.expected_length && !record.candidate_version);
    CHECK(!record.error && record.marker == META_MARKER);
    CHECK(fx.erases == 1 && fx.erase_offsets[0] == 0);
    CHECK(fx.programs == 2); /* body + marker */
    uint8_t page0[META_RECORD_SIZE], page1[META_RECORD_SIZE];
    CHECK(page_record(0, page0) == 0 && page_record(1, page1) == 0);
    meta_record decoded;
    CHECK(meta_decode(page0, &decoded) == META_OK && same_record(&decoded, &record));
    CHECK(all_ff(page1, sizeof(page1)));
    unsigned programs = fx.programs, erases = fx.erases;
    meta_record again;
    CHECK(meta_init(&ops, &again) == META_OK && same_record(&again, &record));
    CHECK(fx.programs == programs && fx.erases == erases);
    CHECK(meta_read(&ops, &again) == META_OK && same_record(&again, &record));
    CHECK(!fx.range_error);
    return 0;
}

static int test_commit_alternates_pages(void) {
    reset_all();
    meta_record record;
    CHECK(meta_init(&ops, &record) == META_OK);
    uint8_t page0[META_RECORD_SIZE];
    CHECK(page_record(0, page0) == 0);
    upgrade_record(&record, META_STATE_RECEIVING, 0, 1000, 7, 0);
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 2);
    CHECK(record.state == META_STATE_RECEIVING && record.target_slot == FLASH_BACKUP);
    CHECK(fx.erase_offsets[1] == META_PAGE_SIZE);
    uint8_t current0[META_RECORD_SIZE];
    CHECK(page_record(0, current0) == 0);
    CHECK(memcmp(page0, current0, sizeof(page0)) == 0); /* old valid page preserved */
    meta_record read;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 2);
    uint8_t page1[META_RECORD_SIZE];
    CHECK(page_record(1, page1) == 0);
    record.progress = 1000;
    record.state = META_STATE_VERIFYING;
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 3);
    CHECK(fx.erase_offsets[2] == 0);
    uint8_t current1[META_RECORD_SIZE];
    CHECK(page_record(1, current1) == 0 && memcmp(page1, current1, sizeof(page1)) == 0);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 3);
    CHECK(read.state == META_STATE_VERIFYING && read.progress == 1000);
    record.state = META_STATE_READY;
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 4);
    record.state = META_STATE_FAILED;
    record.progress = 500;
    record.error = 0x102;
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 5);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 5);
    CHECK(read.state == META_STATE_FAILED && read.progress == 500 && read.error == 0x102);
    CHECK(fx.erase_offsets[3] == META_PAGE_SIZE && fx.erase_offsets[4] == 0);
    CHECK(!fx.range_error);
    return 0;
}

static int test_commit_invalid_fields_rejected(void) {
    reset_all();
    meta_record base, bad;
    CHECK(meta_init(&ops, &base) == META_OK);
    unsigned programs = fx.programs, erases = fx.erases;
    bad = base;
    bad.active_slot = 0;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = base;
    bad.active_slot = 3;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = base;
    bad.state = 9;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = base;
    bad.target_slot = FLASH_BACKUP;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT); /* IDLE needs target NONE */
    bad = base;
    bad.expected_length = 1000;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = base;
    bad.progress = 1;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = base;
    bad.active_slot = FLASH_BACKUP;
    bad.target_slot = FLASH_BACKUP;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT); /* target must not be active */
    upgrade_record(&bad, META_STATE_RECEIVING, 0, 1000, 7, 0);
    meta_record receiving = bad;
    bad = receiving;
    bad.expected_length = META_MIN_PAYLOAD - 1;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.expected_length = META_MAX_PAYLOAD + 1;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.progress = bad.expected_length + 1;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.state = META_STATE_READY;
    bad.progress = bad.expected_length - 1;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.error = 3;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.target_slot = META_SLOT_NONE;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.state = META_STATE_FAILED;
    bad.error = 0;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    bad = receiving;
    bad.state = META_STATE_FAILED;
    bad.error = 5;
    bad.expected_length = 0;
    bad.progress = 1;
    CHECK(meta_commit(&ops, &bad) == META_ARGUMENT);
    CHECK(meta_commit(0, &base) == META_ARGUMENT);
    CHECK(meta_commit(&ops, 0) == META_ARGUMENT);
    CHECK(meta_init(0, &base) == META_ARGUMENT);
    CHECK(meta_read(0, &base) == META_ARGUMENT);
    CHECK(meta_read(&ops, 0) == META_ARGUMENT);
    meta_flash_ops incomplete = ops;
    incomplete.program = 0;
    CHECK(meta_commit(&incomplete, &receiving) == META_ARGUMENT);
    incomplete = ops;
    incomplete.read = 0;
    CHECK(meta_read(&incomplete, &base) == META_ARGUMENT);
    CHECK(meta_commit(&ops, &receiving) == META_OK);
    CHECK(base.generation == 1);
    CHECK(fx.programs == programs + 2 && fx.erases == erases + 1);
    return 0;
}

static void reseal(uint8_t bytes[]) {
    uint32_t crc = crc32(bytes, 40);
    bytes[40] = (uint8_t)crc;
    bytes[41] = (uint8_t)(crc >> 8);
    bytes[42] = (uint8_t)(crc >> 16);
    bytes[43] = (uint8_t)(crc >> 24);
}

static int expect_decode(uint8_t bytes[], int code, int line) {
    meta_record record;
    int result = meta_decode(bytes, &record);
    if (result != code) {
        fprintf(stderr, "decode line %d: got %d expected %d\n", line, result, code);
        return 1;
    }
    return 0;
}

static int test_decode_rejects_fields_and_corruption(void) {
    meta_record good, bad, parsed;
    upgrade_record(&good, META_STATE_RECEIVING, 100, 1000, 7, 0);
    good.generation = 9;
    uint8_t bytes[META_RECORD_SIZE], broken[META_RECORD_SIZE];
    meta_encode(&good, bytes);
    CHECK(meta_decode(bytes, &parsed) == META_OK);
    CHECK(parsed.magic == META_MAGIC && parsed.schema == META_SCHEMA);
    CHECK(parsed.marker == META_MARKER && parsed.generation == 9);
    CHECK(parsed.progress == 100 && parsed.expected_length == 1000);
    CHECK(meta_decode(0, &parsed) == META_ARGUMENT);
    CHECK(meta_decode(bytes, 0) == META_ARGUMENT);
    memcpy(broken, bytes, sizeof(bytes));
    broken[44] ^= 0x01;
    CHECK(expect_decode(broken, META_MARKER_MISSING, __LINE__) == 0);
    memcpy(broken, bytes, sizeof(bytes));
    broken[40] ^= 0x01;
    CHECK(expect_decode(broken, META_CRC, __LINE__) == 0);
    memcpy(broken, bytes, sizeof(bytes));
    broken[0] ^= 0x01;
    reseal(broken);
    CHECK(expect_decode(broken, META_FIELD, __LINE__) == 0);
    memcpy(broken, bytes, sizeof(bytes));
    broken[4] = 2;
    reseal(broken);
    CHECK(expect_decode(broken, META_FIELD, __LINE__) == 0);
    const struct {
        const char *name;
        uint32_t field;
        int value;
        int expected;
    } cases[] = {
        {"generation-zero", 1, 0, META_FIELD},
        {"active-zero", 2, 0, META_FIELD},
        {"active-three", 2, 3, META_FIELD},
        {"target-none", 3, (int)META_SLOT_NONE, META_FIELD},
        {"target-active", 3, FLASH_APP, META_FIELD},
        {"state-nine", 4, 9, META_FIELD},
        {"expected-too-small", 6, 191, META_FIELD},
        {"expected-too-large", 6, (int)META_MAX_PAYLOAD + 1, META_FIELD},
        {"progress-over", 5, 1001, META_FIELD},
        {"error-in-receiving", 8, 3, META_FIELD},
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        bad = good;
        switch (cases[i].field) {
        case 1: bad.generation = (uint32_t)cases[i].value; break;
        case 2: bad.active_slot = (uint32_t)cases[i].value; break;
        case 3: bad.target_slot = (uint32_t)cases[i].value; break;
        case 4: bad.state = (uint32_t)cases[i].value; break;
        case 5: bad.progress = (uint32_t)cases[i].value; break;
        case 6: bad.expected_length = (uint32_t)cases[i].value; break;
        case 8: bad.error = (uint32_t)cases[i].value; break;
        default: CHECK(0);
        }
        meta_encode(&bad, bytes);
        if (expect_decode(bytes, cases[i].expected, __LINE__)) {
            fprintf(stderr, "case %s\n", cases[i].name);
            return 1;
        }
    }
    /* READY with progress == expected and FAILED with a recorded error are legal. */
    bad = good;
    bad.state = META_STATE_READY;
    bad.progress = bad.expected_length;
    meta_encode(&bad, bytes);
    CHECK(meta_decode(bytes, &parsed) == META_OK);
    bad = good;
    bad.state = META_STATE_FAILED;
    bad.progress = 0;
    bad.expected_length = 0;
    bad.error = 5;
    meta_encode(&bad, bytes);
    CHECK(meta_decode(bytes, &parsed) == META_OK);
    idle_record(&bad, 3);
    meta_encode(&bad, bytes);
    CHECK(meta_decode(bytes, &parsed) == META_OK);
    bad.target_slot = FLASH_BACKUP;
    meta_encode(&bad, bytes);
    CHECK(expect_decode(bytes, META_FIELD, __LINE__) == 0);
    return 0;
}

static int test_damaged_pages_require_recovery(void) {
    reset_all();
    meta_record record, read;
    CHECK(meta_init(&ops, &record) == META_OK);
    memory[params_offset() + 40] ^= 0x01; /* record CRC */
    CHECK(meta_read(&ops, &read) == META_RECOVERY_REQUIRED);
    CHECK(meta_init(&ops, &read) == META_RECOVERY_REQUIRED);
    unsigned programs = fx.programs, erases = fx.erases;
    CHECK(meta_init(&ops, &read) == META_RECOVERY_REQUIRED);
    CHECK(fx.programs == programs && fx.erases == erases); /* no silent init */
    reset_all();
    CHECK(meta_init(&ops, &record) == META_OK);
    memory[params_offset() + 44] = 0x00; /* marker */
    CHECK(meta_read(&ops, &read) == META_RECOVERY_REQUIRED);
    reset_all();
    CHECK(meta_init(&ops, &record) == META_OK);
    memory[params_offset()] = 0x00; /* magic */
    CHECK(meta_init(&ops, &read) == META_RECOVERY_REQUIRED);
    reset_all();
    CHECK(meta_init(&ops, &record) == META_OK);
    upgrade_record(&record, META_STATE_RECEIVING, 0, 1000, 4, 0);
    CHECK(meta_commit(&ops, &record) == META_OK);
    memory[params_offset() + 40] ^= 0x01;
    memory[params_offset() + META_PAGE_SIZE + 40] ^= 0x01;
    CHECK(meta_read(&ops, &read) == META_RECOVERY_REQUIRED);
    CHECK(meta_init(&ops, &read) == META_RECOVERY_REQUIRED);
    /* One damaged page must not hide the other valid record. */
    reset_all();
    CHECK(meta_init(&ops, &record) == META_OK);
    upgrade_record(&record, META_STATE_VERIFYING, 1000, 1000, 4, 0);
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 2);
    memory[params_offset() + 40] ^= 0x01;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 2);
    CHECK(meta_init(&ops, &read) == META_OK && read.generation == 2);
    return 0;
}

static int test_io_errors_are_distinct(void) {
    reset_all();
    meta_record record, read;
    fx.fail_read_at = (long)fx.reads + 1;
    CHECK(meta_read(&ops, &read) == META_IO);
    fx.fail_read_at = (long)fx.reads + 1;
    CHECK(meta_init(&ops, &read) == META_IO);
    fx.fail_read_at = (long)fx.reads + 2; /* second page read fails */
    CHECK(meta_read(&ops, &read) == META_IO);
    fx.fail_read_at = 0;
    CHECK(meta_init(&ops, &record) == META_OK);
    unsigned programs = fx.programs;
    fx.fail_erase_at = (long)fx.erases + 1;
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_IO);
    CHECK(fx.programs == programs);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    fx.fail_erase_at = 0;
    fx.fail_program_at = (long)fx.programs + 1;
    fx.program_prefix = 0;
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_IO);
    fx.fail_program_at = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    /* Torn body: first 20 bytes land, the marker never does. */
    fx.fail_program_at = (long)fx.programs + 1;
    fx.program_prefix = 20;
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_IO);
    fx.fail_program_at = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    uint8_t page1[META_RECORD_SIZE];
    CHECK(page_record(1, page1) == 0);
    meta_record partial;
    CHECK(meta_decode(page1, &partial) == META_MARKER_MISSING);
    /* Marker write fails after a verified body. */
    fx.fail_program_at = (long)fx.programs + 2;
    fx.program_prefix = 0;
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_IO);
    fx.fail_program_at = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    /* Lying flash: reported success but the body was not written. */
    fx.noop_program_at = (long)fx.programs + 1;
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_VERIFY);
    fx.noop_program_at = 0;
    /* Lying flash: body persists, marker write is dropped. */
    fx.noop_program_at = (long)fx.programs + 2;
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_VERIFY);
    fx.noop_program_at = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    /* Faults cleared: a normal commit must recover the transaction. */
    upgrade_record(&read, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_OK && read.generation == 2);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 2);
    CHECK(!fx.range_error);
    return 0;
}

static int test_torn_erase_keeps_old_record(void) {
    reset_all();
    meta_record record, read;
    CHECK(meta_init(&ops, &record) == META_OK);
    upgrade_record(&record, META_STATE_RECEIVING, 0, 1000, 5, 0);
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 2);
    uint8_t page1[META_RECORD_SIZE];
    CHECK(page_record(1, page1) == 0);
    /* Next commit targets page 0; interrupt the erase after 2048 bytes. */
    fx.fail_erase_at = (long)fx.erases + 1;
    fx.erase_prefix = 2048;
    upgrade_record(&read, META_STATE_VERIFYING, 1000, 1000, 5, 0);
    CHECK(meta_commit(&ops, &read) == META_IO);
    fx.fail_erase_at = 0;
    fx.erase_prefix = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 2);
    uint8_t current1[META_RECORD_SIZE];
    CHECK(page_record(1, current1) == 0 && memcmp(page1, current1, sizeof(page1)) == 0);
    CHECK(meta_commit(&ops, &record) == META_OK);
    CHECK(meta_commit(&ops, &read) == META_OK && read.generation == 4);
    return 0;
}

static int test_first_init_interrupted(void) {
    reset_all();
    meta_record record, read;
    fx.fail_program_at = 1; /* body fails before any byte lands */
    fx.program_prefix = 0;
    CHECK(meta_init(&ops, &record) == META_IO);
    CHECK(meta_read(&ops, &read) == META_NO_RECORD);
    fx.fail_program_at = 0;
    CHECK(meta_init(&ops, &record) == META_OK && record.generation == 1);
    /* Interrupted marker: the page is no longer blank, so init must not guess. */
    reset_all();
    fx.fail_program_at = 2;
    CHECK(meta_init(&ops, &record) == META_IO);
    CHECK(meta_read(&ops, &read) == META_RECOVERY_REQUIRED);
    CHECK(meta_init(&ops, &read) == META_RECOVERY_REQUIRED);
    unsigned programs = fx.programs, erases = fx.erases;
    CHECK(meta_init(&ops, &read) == META_RECOVERY_REQUIRED);
    CHECK(fx.programs == programs && fx.erases == erases);
    /* Explicit recovery write is a caller decision, not silent initialisation. */
    idle_record(&record, 0);
    CHECK(meta_commit(&ops, &record) == META_OK && record.generation == 1);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    return 0;
}

static int test_generation_wrap(void) {
    reset_all();
    meta_record older, newer, read;
    idle_record(&older, 0xFFFFFFFEu);
    CHECK(write_page_record(0, &older) == 0);
    idle_record(&newer, 0xFFFFFFFFu);
    CHECK(write_page_record(1, &newer) == 0);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 0xFFFFFFFFu);
    meta_record wrapped;
    idle_record(&wrapped, 0);
    CHECK(meta_commit(&ops, &wrapped) == META_OK && wrapped.generation == 1);
    CHECK(fx.erase_offsets[0] == 0); /* newest was page 1 */
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    /* The pre-wrap record on page 1 must still be intact. */
    uint8_t page1[META_RECORD_SIZE];
    CHECK(page_record(1, page1) == 0);
    meta_record decoded;
    CHECK(meta_decode(page1, &decoded) == META_OK &&
          decoded.generation == 0xFFFFFFFFu);
    /* Near-half ordering stays deterministic. */
    reset_all();
    idle_record(&older, 0x7FFFFFFDu);
    CHECK(write_page_record(0, &older) == 0);
    idle_record(&newer, 0xFFFFFFFEu);
    CHECK(write_page_record(1, &newer) == 0);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 0x7FFFFFFDu);
    return 0;
}

static int test_generation_ambiguity(void) {
    reset_all();
    meta_record first, second, read;
    idle_record(&first, 5);
    CHECK(write_page_record(0, &first) == 0);
    idle_record(&second, 5);
    CHECK(write_page_record(1, &second) == 0);
    CHECK(meta_read(&ops, &read) == META_AMBIGUOUS);
    CHECK(meta_init(&ops, &read) == META_AMBIGUOUS);
    meta_record next;
    idle_record(&next, 0);
    unsigned programs = fx.programs, erases = fx.erases;
    CHECK(meta_commit(&ops, &next) == META_AMBIGUOUS);
    CHECK(fx.programs == programs && fx.erases == erases);
    reset_all();
    idle_record(&first, 1);
    CHECK(write_page_record(0, &first) == 0);
    idle_record(&second, 0x80000001u);
    CHECK(write_page_record(1, &second) == 0);
    CHECK(meta_read(&ops, &read) == META_AMBIGUOUS);
    reset_all();
    idle_record(&first, 0x80000001u);
    CHECK(write_page_record(0, &first) == 0);
    idle_record(&second, 1);
    CHECK(write_page_record(1, &second) == 0);
    CHECK(meta_read(&ops, &read) == META_AMBIGUOUS);
    /* Just below half range has a deterministic winner. */
    reset_all();
    idle_record(&first, 1);
    CHECK(write_page_record(0, &first) == 0);
    idle_record(&second, 0x80000002u);
    CHECK(write_page_record(1, &second) == 0);
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
    return 0;
}

static int test_firmware_regions_untouched(void) {
    reset_all();
    meta_record record;
    CHECK(meta_init(&ops, &record) == META_OK);
    upgrade_record(&record, META_STATE_RECEIVING, 0, 1000, 3, 0);
    CHECK(meta_commit(&ops, &record) == META_OK);
    record.progress = 1000;
    record.state = META_STATE_VERIFYING;
    CHECK(meta_commit(&ops, &record) == META_OK);
    record.state = META_STATE_READY;
    CHECK(meta_commit(&ops, &record) == META_OK);
    record.state = 9;
    CHECK(meta_commit(&ops, &record) == META_ARGUMENT);
    CHECK(crc32(memory, FIRMWARE_SIZE) == firmware_crc);
    CHECK(!fx.range_error);
    for (unsigned i = 0; i < fx.erase_count; ++i)
        CHECK(fx.erase_offsets[i] == 0 || fx.erase_offsets[i] == META_PAGE_SIZE);
    return 0;
}


static int test_byte_tears(void) {
    /* Body bytes 0..44: in every case the old record stays readable and the
       damaged page never decodes as a committed record. */
    for (uint32_t torn = 0; torn <= 44; ++torn) {
        reset_all();
        meta_record record, read, pending;
        CHECK(meta_init(&ops, &record) == META_OK);
        uint8_t old_page[META_RECORD_SIZE];
        CHECK(page_record(0, old_page) == 0);
        upgrade_record(&pending, META_STATE_RECEIVING, 0, 1000, 9, 0);
        meta_record expected = pending;
        expected.generation = 2;
        uint8_t encoded[META_RECORD_SIZE];
        meta_encode(&expected, encoded);
        fx.fail_program_at = (long)fx.programs + 1;
        fx.program_prefix = torn;
        CHECK(meta_commit(&ops, &pending) == META_IO);
        fx.fail_program_at = 0;
        fx.program_prefix = 0;
        CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
        uint8_t current0[META_RECORD_SIZE];
        CHECK(page_record(0, current0) == 0);
        CHECK(memcmp(old_page, current0, sizeof(old_page)) == 0);
        uint8_t target[META_RECORD_SIZE];
        CHECK(page_record(1, target) == 0);
        for (uint32_t i = 0; i < torn; ++i) CHECK(target[i] == encoded[i]);
        for (uint32_t i = torn; i < META_RECORD_SIZE; ++i)
            CHECK(target[i] == 0xFF); /* nothing past the tear landed */
        CHECK(meta_commit(&ops, &pending) == META_OK && pending.generation == 2);
    }
    return 0;
}

static int test_marker_tears(void) {
    /* Marker bytes 0..3: body plus CRC are complete, the commit marker is not,
       so the record must stay invalid and the old generation must survive. */
    for (uint32_t torn = 0; torn <= 3; ++torn) {
        reset_all();
        meta_record record, read, pending;
        CHECK(meta_init(&ops, &record) == META_OK);
        upgrade_record(&pending, META_STATE_RECEIVING, 0, 1000, 9, 0);
        meta_record expected = pending;
        expected.generation = 2;
        uint8_t encoded[META_RECORD_SIZE];
        meta_encode(&expected, encoded);
        fx.fail_program_at = (long)fx.programs + 2;
        fx.program_prefix = torn;
        CHECK(meta_commit(&ops, &pending) == META_IO);
        fx.fail_program_at = 0;
        fx.program_prefix = 0;
        CHECK(meta_read(&ops, &read) == META_OK && read.generation == 1);
        uint8_t target[META_RECORD_SIZE];
        CHECK(page_record(1, target) == 0);
        CHECK(memcmp(target, encoded, 44) == 0);
        for (uint32_t i = 0; i < torn; ++i)
            CHECK(target[44 + i] == encoded[44 + i]);
        for (uint32_t i = 44 + torn; i < META_RECORD_SIZE; ++i)
            CHECK(target[i] == 0xFF); /* the marker write stopped at the tear */
    }
    /* All four marker bytes landed before the failure was reported: the record
       is complete and must be visible even though commit returned an error. */
    reset_all();
    meta_record record, read, pending;
    CHECK(meta_init(&ops, &record) == META_OK);
    upgrade_record(&pending, META_STATE_RECEIVING, 0, 1000, 9, 0);
    fx.fail_program_at = (long)fx.programs + 2;
    fx.program_prefix = 4;
    CHECK(meta_commit(&ops, &pending) == META_IO);
    fx.fail_program_at = 0;
    fx.program_prefix = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 2);
    CHECK(read.state == META_STATE_RECEIVING && read.expected_length == 1000);
    return 0;
}

static int test_commit_readback_io(void) {
    /* The marker reached the medium but the final read-back failed. commit
       reports IO; a fresh read must return the complete new record. */
    reset_all();
    meta_record record, read, pending;
    CHECK(meta_init(&ops, &record) == META_OK);
    upgrade_record(&pending, META_STATE_RECEIVING, 0, 1000, 9, 0);
    fx.fail_read_at = (long)fx.reads + 4; /* scan 2, body check, final check */
    CHECK(meta_commit(&ops, &pending) == META_IO);
    fx.fail_read_at = 0;
    CHECK(meta_read(&ops, &read) == META_OK && read.generation == 2);
    CHECK(read.state == META_STATE_RECEIVING && read.expected_length == 1000);
    return 0;
}

static int test_record_layout(void) {
    meta_record record;
    memset(&record, 0, sizeof(record));
    record.generation = 0x0A0B0C0Du;
    record.active_slot = FLASH_BACKUP;
    record.target_slot = FLASH_APP;
    record.state = META_STATE_RECEIVING;
    record.progress = 5;
    record.expected_length = 0x0000ABCDu;
    record.candidate_version = 0x55667788u;
    uint8_t bytes[META_RECORD_SIZE];
    meta_encode(&record, bytes);
    CHECK(read_le32(bytes + 0) == META_MAGIC);
    CHECK(read_le32(bytes + 4) == META_SCHEMA);
    CHECK(read_le32(bytes + 8) == 0x0A0B0C0Du);
    CHECK(read_le32(bytes + 12) == FLASH_BACKUP);
    CHECK(read_le32(bytes + 16) == FLASH_APP);
    CHECK(read_le32(bytes + 20) == META_STATE_RECEIVING);
    CHECK(read_le32(bytes + 24) == 5);
    CHECK(read_le32(bytes + 28) == 0x0000ABCDu);
    CHECK(read_le32(bytes + 32) == 0x55667788u);
    CHECK(read_le32(bytes + 36) == 0);
    CHECK(read_le32(bytes + 40) == crc32(bytes, 40));
    CHECK(read_le32(bytes + 44) == META_MARKER);
    return 0;
}

static int test_production_adapter(void) {
    reset_all();
    meta_flash_ops real;
    meta_flash_bind(&real, &fx.dev);
    meta_record record, read;
    CHECK(meta_read(&real, &read) == META_NO_RECORD);
    CHECK(meta_init(&real, &record) == META_OK && record.generation == 1);
    upgrade_record(&record, META_STATE_RECEIVING, 0, 512, 2, 0);
    CHECK(meta_commit(&real, &record) == META_OK && record.generation == 2);
    CHECK(meta_read(&real, &read) == META_OK && same_record(&read, &record));
    CHECK(crc32(memory, FIRMWARE_SIZE) == firmware_crc);
    meta_flash_ops unbound;
    memset(&unbound, 0, sizeof(unbound));
    meta_flash_bind(&unbound, 0);
    CHECK(meta_read(&unbound, &read) == META_IO); /* NULL device -> IO, not NO_RECORD */
    return 0;
}

int main(void) {
    static const struct { const char *name; int (*run)(void); } tests[] = {
        {"blank-then-init", test_blank_then_init},
        {"commit-alternates-pages", test_commit_alternates_pages},
        {"commit-invalid-fields", test_commit_invalid_fields_rejected},
        {"decode-fields-and-corruption", test_decode_rejects_fields_and_corruption},
        {"damaged-pages-need-recovery", test_damaged_pages_require_recovery},
        {"io-errors-distinct", test_io_errors_are_distinct},
        {"torn-erase-keeps-old", test_torn_erase_keeps_old_record},
        {"first-init-interrupted", test_first_init_interrupted},
        {"generation-wrap", test_generation_wrap},
        {"generation-ambiguity", test_generation_ambiguity},
        {"firmware-regions-untouched", test_firmware_regions_untouched},
        {"byte-tears", test_byte_tears},
        {"marker-tears", test_marker_tears},
        {"commit-readback-io", test_commit_readback_io},
        {"record-layout", test_record_layout},
        {"production-adapter", test_production_adapter},
    };
    CHECK(params_size() == PARAMS_SIZE);
    for (unsigned i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        if (tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
    }
    puts("PASS: boot_meta init/read/commit, damage, IO, torn writes, wrap, recovery");
    return 0;
}
