#include <string.h>
#include "boot_config.h"
#include "crc32.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static int valid_fields(const cfg_record *record) {
    if (record->window_ms < CFG_MIN_WINDOW_MS ||
        record->window_ms > CFG_MAX_WINDOW_MS) return 0;
    if (record->auto_boot > 1u) return 0;
    if (record->trial_pending > 1u) return 0;
    if (record->trial_stage > CFG_TRIAL_LAUNCHED) return 0;
    if (record->trial_slot != META_SLOT_NONE &&
        record->trial_slot != FLASH_APP &&
        record->trial_slot != FLASH_BACKUP) return 0;
    if (record->last_confirmed_slot != META_SLOT_NONE &&
        record->last_confirmed_slot != FLASH_APP &&
        record->last_confirmed_slot != FLASH_BACKUP) return 0;
    return 1;
}

void cfg_defaults(cfg_record *record) {
    unsigned i;
    if (!record) return;
    record->magic = CFG_MAGIC;
    record->schema = CFG_SCHEMA;
    record->generation = 1;
    record->window_ms = CFG_DEFAULT_WINDOW_MS;
    record->auto_boot = 1;
    record->trial_slot = META_SLOT_NONE;
    record->last_confirmed_slot = META_SLOT_NONE;
    record->trial_pending = 0;
    record->trial_stage = CFG_TRIAL_NONE;
    for (i = 0; i < 3; ++i) record->reserved[i] = 0;
    record->crc = record->marker = 0;
}

static void encode(const cfg_record *record, uint8_t bytes[CFG_RECORD_SIZE]) {
    unsigned i;
    uint32_t fields[14] = {
        CFG_MAGIC, CFG_SCHEMA, record->generation, record->window_ms,
        record->auto_boot, record->trial_slot, record->last_confirmed_slot,
        record->trial_pending, record->trial_stage, record->reserved[0],
        record->reserved[1], record->reserved[2], 0, CFG_MARKER
    };
    fields[12] = crc32(fields, 48); /* first 12 words */
    /* crc32() over uint32_t[] is endian-dependent on a little-endian host
       and on this Cortex-M target, which is also little-endian. Encode
       explicitly so a host sanitizer test can share the layout. */
    for (i = 0; i < 14; ++i) put_le32(bytes + 4u * i, fields[i]);
    /* Recompute CRC over the serialized first 48 bytes. */
    put_le32(bytes + 48, crc32(bytes, 48));
}

static int decode(const uint8_t bytes[CFG_RECORD_SIZE], cfg_record *record) {
    unsigned i;
    uint32_t words[14];
    int blank = 1;
    for (i = 0; i < CFG_RECORD_SIZE; ++i)
        if (bytes[i] != 0xffu) blank = 0;
    if (blank) return CFG_NO_RECORD;
    for (i = 0; i < 14; ++i) words[i] = le32(bytes + 4u * i);
    if (words[0] != CFG_MAGIC || words[1] != CFG_SCHEMA ||
        words[13] != CFG_MARKER) return CFG_VERIFY;
    if (crc32(bytes, 48) != words[12]) return CFG_VERIFY;
    record->magic = words[0];
    record->schema = words[1];
    record->generation = words[2];
    record->window_ms = words[3];
    record->auto_boot = words[4];
    record->trial_slot = words[5];
    record->last_confirmed_slot = words[6];
    record->trial_pending = words[7];
    record->trial_stage = words[8];
    for (i = 0; i < 3; ++i) record->reserved[i] = words[9 + i];
    record->crc = words[12];
    record->marker = words[13];
    if (!record->generation || !valid_fields(record)) return CFG_VERIFY;
    return CFG_OK;
}

static uint32_t next_generation(uint32_t current) {
    return current == 0xffffffffu ? 1u : current + 1u;
}

static int read_page(const meta_flash_ops *ops, unsigned page, cfg_record *record) {
    uint8_t bytes[CFG_RECORD_SIZE];
    uint32_t offset = CFG_PAGE_BASE + page * META_PAGE_SIZE;
    if (ops->read(ops->context, offset, bytes, sizeof(bytes))) return CFG_IO;
    return decode(bytes, record);
}

int cfg_read(const meta_flash_ops *ops, cfg_record *record) {
    cfg_record first, second;
    int a, b;
    if (!ops || !ops->read || !record) return CFG_ARGUMENT;
    a = read_page(ops, 0, &first);
    b = read_page(ops, 1, &second);
    if (a == CFG_IO || b == CFG_IO) return CFG_IO;
    if (a != CFG_OK && b != CFG_OK)
        return (a == CFG_NO_RECORD && b == CFG_NO_RECORD) ? CFG_NO_RECORD : CFG_VERIFY;
    if (a == CFG_OK && b != CFG_OK) { *record = first; return CFG_OK; }
    if (b == CFG_OK && a != CFG_OK) { *record = second; return CFG_OK; }
    if (first.generation == second.generation) return CFG_AMBIGUOUS;
    /* Newer generation wins, including wrap from UINT32_MAX to 1 when the
       other value is UINT32_MAX. */
    if (first.generation == 1u && second.generation == 0xffffffffu)
        *record = first;
    else if (second.generation == 1u && first.generation == 0xffffffffu)
        *record = second;
    else
        *record = first.generation > second.generation ? first : second;
    return CFG_OK;
}

int cfg_commit(const meta_flash_ops *ops, cfg_record *record) {
    cfg_record current;
    uint8_t bytes[CFG_RECORD_SIZE], check[CFG_RECORD_SIZE];
    unsigned page = 0;
    uint32_t offset, generation = 1;
    int status;
    if (!ops || !ops->read || !ops->program || !ops->erase || !record)
        return CFG_ARGUMENT;
    if (!valid_fields(record)) return CFG_ARGUMENT;
    status = cfg_read(ops, &current);
    if (status == CFG_IO) return CFG_IO;
    if (status == CFG_AMBIGUOUS) return CFG_AMBIGUOUS;
    if (status == CFG_OK) {
        generation = next_generation(current.generation);
        /* Write the page that does not hold the current record. */
        page = 1;
        {
            cfg_record probe;
            if (read_page(ops, 0, &probe) == CFG_OK &&
                probe.generation == current.generation)
                page = 1;
            else
                page = 0;
        }
    }
    record->magic = CFG_MAGIC;
    record->schema = CFG_SCHEMA;
    record->generation = generation;
    encode(record, bytes);
    offset = CFG_PAGE_BASE + page * META_PAGE_SIZE;
    if (ops->erase(ops->context, offset, META_PAGE_SIZE)) return CFG_IO;
    /* 13 words (fields + CRC) first, commit marker last. */
    if (ops->program(ops->context, offset, bytes, 52)) return CFG_IO;
    if (ops->read(ops->context, offset, check, 52) ||
        memcmp(bytes, check, 52) != 0) return CFG_VERIFY;
    if (ops->program(ops->context, offset + 52, bytes + 52, 4)) return CFG_IO;
    if (ops->read(ops->context, offset, check, CFG_RECORD_SIZE) ||
        memcmp(bytes, check, CFG_RECORD_SIZE) != 0) return CFG_VERIFY;
    if (decode(check, record) != CFG_OK) return CFG_VERIFY;
    return CFG_OK;
}

int cfg_init(const meta_flash_ops *ops, cfg_record *record) {
    int status;
    cfg_record fresh;
    if (!ops || !record) return CFG_ARGUMENT;
    status = cfg_read(ops, record);
    if (status == CFG_OK || status == CFG_IO || status == CFG_AMBIGUOUS)
        return status;
    if (status != CFG_NO_RECORD && status != CFG_VERIFY) return status;
    /* Damaged pages are recovered only by an explicit factory reset or this
       first-time initialisation when at least one page was blank and the
       other failed decode? If any damage exists, still refuse to guess and
       require factory/reset except when both pages are blank. */
    if (status != CFG_NO_RECORD) return status;
    cfg_defaults(&fresh);
    status = cfg_commit(ops, &fresh);
    if (status != CFG_OK) return status;
    *record = fresh;
    return CFG_OK;
}
