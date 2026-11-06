#ifndef WEEK5_BOOT_META_H
#define WEEK5_BOOT_META_H
#include "flash.h"

/* Week-5 upgrade metadata, stored as two independent 4 KiB pages inside
 * FLASH_PARAMS. The record layout is fixed little-endian:
 *
 *   offset  field
 *   0       magic        "M5BM"
 *   4       schema       1
 *   8       generation   monotonic, 1..UINT32_MAX, wraps to 1
 *   12      active_slot  FLASH_APP or FLASH_BACKUP
 *   16      target_slot  a slot, or META_SLOT_NONE for IDLE
 *   20      state        META_STATE_*
 *   24      progress     verified bytes written, <= expected_length
 *   28      expected_length  payload bytes, 0 only when unknown/IDLE
 *   32      candidate_version
 *   36      error        non-zero only for FAILED
 *   40      crc          CRC-32 over bytes 0..39 (zlib polynomial)
 *   44      marker       "CMIT", written last
 *
 * A page is a valid record only when magic, schema, all field rules, the CRC
 * and the commit marker agree. "Blank" means the 48-byte record area at the
 * start of that page is all 0xFF; the remaining 4 KiB are not inspected, so a
 * genuinely erased page and a page whose record area only was erased look the
 * same. Anything else that fails validation is damage, never "no record". */
#define META_MAGIC 0x4d42354du   /* little-endian "M5BM" */
#define META_SCHEMA 1u
#define META_MARKER 0x54494d43u  /* little-endian "CMIT" */
#define META_RECORD_SIZE 48u
#define META_PAGE_SIZE FLASH_ERASE_SIZE
#define META_PAGE_COUNT 2u
#define META_SLOT_NONE 0xffffffffu
/* Must match APP_VECTOR_SIZE and SLOT_SIZE - IMAGE_HEADER_SIZE in image.h. */
#define META_MIN_PAYLOAD 192u
#define META_MAX_PAYLOAD 0x6ffe0u

typedef enum {
    META_OK = 0,            /* record read, committed or initialised */
    META_IO = 1,            /* storage read/program/erase failed */
    META_NO_RECORD = 2,     /* both pages blank; caller may initialise */
    META_RECOVERY_REQUIRED = 3, /* damaged pages and no valid record */
    META_ARGUMENT = 4,      /* API misuse or invalid record fields on commit */
    META_VERIFY = 5,        /* read-back did not match what was written */
    META_AMBIGUOUS = 6,     /* two valid records that cannot be ordered */
    META_CRC = 7,           /* record CRC mismatch */
    META_FIELD = 8,         /* illegal field value */
    META_MARKER_MISSING = 9 /* commit marker missing */
} meta_result;

typedef enum {
    META_STATE_IDLE = 0,
    META_STATE_RECEIVING = 1,
    META_STATE_VERIFYING = 2,
    META_STATE_READY = 3,
    META_STATE_FAILED = 4
} meta_state;

typedef struct {
    uint32_t magic, schema, generation, active_slot, target_slot, state,
             progress, expected_length, candidate_version, error, crc, marker;
} meta_record;

/* Storage adapter. Production code binds it to the Week-3 NOR model with
 * meta_flash_bind(); tests substitute wrappers that inject failures. Offsets
 * are relative to the start of FLASH_PARAMS and must stay inside it. */
typedef struct {
    void *context;
    int (*read)(void *context, uint32_t offset, void *output, uint32_t size);
    int (*program)(void *context, uint32_t offset, const void *input, uint32_t size);
    int (*erase)(void *context, uint32_t offset, uint32_t size);
} meta_flash_ops;

/* Bind ops to a caller-owned simulated NOR device. */
void meta_flash_bind(meta_flash_ops *ops, flash_device *dev);

/* Pure serialization helpers. meta_encode() stamps magic, schema, CRC and the
 * commit marker; it does not validate the semantic fields, so callers can build
 * damaged images for tests and recovery tools. meta_decode() validates fully. */
void meta_encode(const meta_record *record, uint8_t bytes[META_RECORD_SIZE]);
int meta_decode(const uint8_t bytes[META_RECORD_SIZE], meta_record *record);

/* Newest valid record. META_IO means the state is unknown, not empty: callers
 * must re-read after an IO error and must never write a firmware slot based on
 * an active_slot remembered from before the error.
 * Returns META_NO_RECORD for a blank area,
 * META_RECOVERY_REQUIRED for damaged pages without a valid record and
 * META_AMBIGUOUS when two valid records cannot be ordered. */
int meta_read(const meta_flash_ops *ops, meta_record *record);

/* Read; on a blank area write the first record, IDLE with active_slot = A and
 * generation 1. Never initialises over damaged pages: those return
 * META_RECOVERY_REQUIRED so the caller must choose an explicit recovery path.
 * Idempotent when a valid record already exists. */
int meta_init(const meta_flash_ops *ops, meta_record *record);

/* Commit record to the page that does not hold the current valid record:
 * erase that page, write body + CRC, read it back, write the marker last, then
 * read and validate the whole record. generation is assigned here; generation,
 * magic, schema, crc and marker in the input are ignored. Returns
 * META_AMBIGUOUS without writing when two valid records cannot be ordered.
 * On a blank or damaged area this is the caller's explicit recovery write:
 * page 0 is erased and the new record starts again at generation 1. */
int meta_commit(const meta_flash_ops *ops, meta_record *record);
#endif
