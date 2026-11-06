#include "boot_meta.h"
#include "crc32.h"

/* The on-flash layout is 12 little-endian words; no padding, no struct copy. */
typedef char meta_record_size_check[
    (sizeof(meta_record) == META_RECORD_SIZE) ? 1 : -1];

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void encode_fields(const meta_record *record, uint8_t bytes[40]) {
    put32(bytes + 0, META_MAGIC);
    put32(bytes + 4, META_SCHEMA);
    put32(bytes + 8, record->generation);
    put32(bytes + 12, record->active_slot);
    put32(bytes + 16, record->target_slot);
    put32(bytes + 20, record->state);
    put32(bytes + 24, record->progress);
    put32(bytes + 28, record->expected_length);
    put32(bytes + 32, record->candidate_version);
    put32(bytes + 36, record->error);
}

static void encode_body(const meta_record *record, uint8_t bytes[44]) {
    encode_fields(record, bytes);
    put32(bytes + 40, crc32(bytes, 40));
}

void meta_encode(const meta_record *record, uint8_t bytes[META_RECORD_SIZE]) {
    if (!record || !bytes) return;
    encode_body(record, bytes);
    put32(bytes + 44, META_MARKER);
}

static int field_rules(const meta_record *record) {
    if (record->active_slot != FLASH_APP && record->active_slot != FLASH_BACKUP)
        return META_FIELD;
    if (record->target_slot != META_SLOT_NONE &&
        record->target_slot != FLASH_APP && record->target_slot != FLASH_BACKUP)
        return META_FIELD;
    /* An upgrade always targets the slot that is not active. */
    if (record->target_slot == record->active_slot) return META_FIELD;
    switch (record->state) {
    case META_STATE_IDLE:
        if (record->target_slot != META_SLOT_NONE || record->progress ||
            record->expected_length || record->candidate_version || record->error)
            return META_FIELD;
        return META_OK;
    case META_STATE_RECEIVING:
    case META_STATE_VERIFYING:
    case META_STATE_READY:
        if (record->target_slot == META_SLOT_NONE) return META_FIELD;
        if (record->expected_length < META_MIN_PAYLOAD ||
            record->expected_length > META_MAX_PAYLOAD) return META_FIELD;
        if (record->progress > record->expected_length) return META_FIELD;
        if (record->state == META_STATE_READY &&
            record->progress != record->expected_length) return META_FIELD;
        if (record->error) return META_FIELD;
        return META_OK;
    case META_STATE_FAILED:
        if (record->target_slot == META_SLOT_NONE) return META_FIELD;
        if (record->expected_length == 0) {
            if (record->progress) return META_FIELD;
        } else if (record->expected_length < META_MIN_PAYLOAD ||
                   record->expected_length > META_MAX_PAYLOAD ||
                   record->progress > record->expected_length) {
            return META_FIELD;
        }
        if (!record->error) return META_FIELD;
        return META_OK;
    default:
        return META_FIELD;
    }
}

int meta_decode(const uint8_t bytes[META_RECORD_SIZE], meta_record *record) {
    if (!bytes || !record) return META_ARGUMENT;
    meta_record parsed;
    parsed.magic = get32(bytes + 0);
    parsed.schema = get32(bytes + 4);
    parsed.generation = get32(bytes + 8);
    parsed.active_slot = get32(bytes + 12);
    parsed.target_slot = get32(bytes + 16);
    parsed.state = get32(bytes + 20);
    parsed.progress = get32(bytes + 24);
    parsed.expected_length = get32(bytes + 28);
    parsed.candidate_version = get32(bytes + 32);
    parsed.error = get32(bytes + 36);
    parsed.crc = get32(bytes + 40);
    parsed.marker = get32(bytes + 44);
    if (parsed.marker != META_MARKER) return META_MARKER_MISSING;
    if (crc32(bytes, 40) != parsed.crc) return META_CRC;
    if (parsed.magic != META_MAGIC || parsed.schema != META_SCHEMA) return META_FIELD;
    if (parsed.generation == 0) return META_FIELD;
    int rules = field_rules(&parsed);
    if (rules != META_OK) return rules;
    *record = parsed;
    return META_OK;
}

static int flash_read_adapter(void *context, uint32_t offset, void *output,
                              uint32_t size) {
    return flash_read(context, FLASH_PARAMS, offset, output, size);
}

static int flash_program_adapter(void *context, uint32_t offset,
                                 const void *input, uint32_t size) {
    return flash_program(context, FLASH_PARAMS, offset, input, size);
}

static int flash_erase_adapter(void *context, uint32_t offset, uint32_t size) {
    return flash_erase(context, FLASH_PARAMS, offset, size);
}

void meta_flash_bind(meta_flash_ops *ops, flash_device *dev) {
    if (!ops) return;
    ops->context = dev;
    ops->read = flash_read_adapter;
    ops->program = flash_program_adapter;
    ops->erase = flash_erase_adapter;
}

typedef struct {
    meta_record record;
    unsigned page;
    unsigned other_page;
    int valid;
    int blank;
} page_state;

static int load_page(const meta_flash_ops *ops, unsigned page, page_state *state) {
    uint8_t bytes[META_RECORD_SIZE];
    state->page = page;
    state->other_page = 1u - page;
    state->valid = 0;
    state->blank = 1;
    if (ops->read(ops->context, page * META_PAGE_SIZE, bytes, sizeof(bytes)))
        return META_IO;
    /* "Blank" is only the record area at the page start, matching what the
       reader actually consumes; later bytes of the 4 KiB page are ignored. */
    for (unsigned i = 0; i < META_RECORD_SIZE; ++i) {
        if (bytes[i] != 0xFFu) { state->blank = 0; break; }
    }
    if (!state->blank && meta_decode(bytes, &state->record) == META_OK)
        state->valid = 1;
    return META_OK;
}

/* Returns META_OK when at least one record is valid and orderable. */
static int scan_area(const meta_flash_ops *ops, page_state *first,
                     page_state *second) {
    if (load_page(ops, 0, first) != META_OK) return META_IO;
    if (load_page(ops, 1, second) != META_OK) return META_IO;
    int valid = first->valid + second->valid;
    if (valid == 0)
        return (first->blank && second->blank) ? META_NO_RECORD
                                                : META_RECOVERY_REQUIRED;
    if (valid == 1) return META_OK;
    if (first->record.generation == second->record.generation)
        return META_AMBIGUOUS;
    /* Wrap-aware order: newer when the unsigned difference is below 2^31. */
    if (first->record.generation - second->record.generation == 0x80000000u)
        return META_AMBIGUOUS;
    return META_OK;
}

static const page_state *newest_page(const page_state *first,
                                     const page_state *second) {
    if (!first->valid) return second;
    if (!second->valid) return first;
    return first->record.generation - second->record.generation < 0x80000000u
               ? first : second;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int records_equal(const meta_record *a, const meta_record *b) {
    return a->magic == b->magic && a->schema == b->schema &&
           a->generation == b->generation &&
           a->active_slot == b->active_slot &&
           a->target_slot == b->target_slot && a->state == b->state &&
           a->progress == b->progress &&
           a->expected_length == b->expected_length &&
           a->candidate_version == b->candidate_version &&
           a->error == b->error && a->crc == b->crc && a->marker == b->marker;
}

int meta_read(const meta_flash_ops *ops, meta_record *record) {
    if (!ops || !record || !ops->read) return META_ARGUMENT;
    page_state first, second;
    int status = scan_area(ops, &first, &second);
    if (status != META_OK) return status;
    *record = newest_page(&first, &second)->record;
    return META_OK;
}

int meta_init(const meta_flash_ops *ops, meta_record *record) {
    if (!ops || !record) return META_ARGUMENT;
    meta_record current;
    int status = meta_read(ops, &current);
    if (status == META_OK) {
        *record = current;
        return META_OK;
    }
    if (status != META_NO_RECORD) return status;
    meta_record fresh;
    fresh.magic = META_MAGIC;
    fresh.schema = META_SCHEMA;
    fresh.generation = 1;
    fresh.active_slot = FLASH_APP;
    fresh.target_slot = META_SLOT_NONE;
    fresh.state = META_STATE_IDLE;
    fresh.progress = 0;
    fresh.expected_length = 0;
    fresh.candidate_version = 0;
    fresh.error = 0;
    fresh.crc = 0;
    fresh.marker = META_MARKER;
    status = meta_commit(ops, &fresh);
    if (status == META_OK) *record = fresh;
    return status;
}

int meta_commit(const meta_flash_ops *ops, meta_record *record) {
    if (!ops || !record || !ops->read || !ops->program || !ops->erase)
        return META_ARGUMENT;
    if (field_rules(record) != META_OK) return META_ARGUMENT;
    page_state first, second;
    int status = scan_area(ops, &first, &second);
    if (status == META_IO || status == META_AMBIGUOUS) return status;
    const page_state *current = status == META_OK ? newest_page(&first, &second) : 0;
    /* Damaged or blank area: the caller explicitly asked for this record, so
     * deterministic recovery writes page 0 with generation 1. */
    unsigned page = current ? current->other_page : 0u;
    uint32_t generation = 1u;
    if (current)
        generation = current->record.generation == UINT32_MAX
                         ? 1u : current->record.generation + 1u;
    meta_record pending;
    pending.magic = META_MAGIC;
    pending.schema = META_SCHEMA;
    pending.generation = generation;
    pending.active_slot = record->active_slot;
    pending.target_slot = record->target_slot;
    pending.state = record->state;
    pending.progress = record->progress;
    pending.expected_length = record->expected_length;
    pending.candidate_version = record->candidate_version;
    pending.error = record->error;
    pending.crc = 0;
    pending.marker = META_MARKER;
    uint8_t body[44];
    encode_body(&pending, body);
    pending.crc = get32(body + 40);
    uint32_t base = page * META_PAGE_SIZE;
    if (ops->erase(ops->context, base, META_PAGE_SIZE)) return META_IO;
    if (ops->program(ops->context, base, body, sizeof(body))) return META_IO;
    uint8_t check[44];
    if (ops->read(ops->context, base, check, sizeof(check))) return META_IO;
    if (!bytes_equal(check, body, sizeof(body))) return META_VERIFY;
    uint8_t marker[4];
    put32(marker, META_MARKER);
    if (ops->program(ops->context, base + 44u, marker, sizeof(marker)))
        return META_IO;
    uint8_t full[META_RECORD_SIZE];
    if (ops->read(ops->context, base, full, sizeof(full))) return META_IO;
    meta_record readback;
    if (meta_decode(full, &readback) != META_OK) return META_VERIFY;
    if (!records_equal(&readback, &pending)) return META_VERIFY;
    *record = pending;
    return META_OK;
}
