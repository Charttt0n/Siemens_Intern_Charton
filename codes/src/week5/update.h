/** @file
 * @brief Receive, verify and activate a firmware candidate in the inactive slot.
 */
#ifndef WEEK5_UPDATE_H
#define WEEK5_UPDATE_H
#include "flash.h"
#include "image.h"
#include "boot_meta.h"
#include "ymodem.h"

/** Upload transaction: receive one firmware image over YMODEM and write it to the
 * slot that is not active. The active_slot is never changed here; a fully
 * validated image is only recorded as READY, which update_activate() commits.
 *
 * Transaction order for a transfer:
 *   1. update_prepare() reads the metadata (a blank area becomes IDLE/A) and
 *      selects target = the slot other than active_slot.
 *   2. YMODEM block 0 -> persist RECEIVING first, then erase the target slot.
 *   3. Data is programmed in 4 byte words; the last word is padded with 0xFF.
 *      Each accepted block persists the RECEIVING progress.
 *   4. YMODEM finish -> validate length, header, payload CRC32, address and
 *      vectors (and signature for the Week-6/7 signed receiver), then persist READY (active_slot unchanged).
 *   5. Any failure or abort persists FAILED with a non-zero error and leaves
 *      the active slot untouched. */
typedef enum {
    UPDATE_OK = 0,
    UPDATE_IO = 1,
    UPDATE_ARGUMENT = 2,
    UPDATE_LENGTH = 3,
    UPDATE_IMAGE = 4,
    UPDATE_STATE = 5,
    UPDATE_ERASE = 6,
    UPDATE_WRITE = 7,
    UPDATE_META = 8,
    UPDATE_ABORTED = 9
} update_result;

typedef struct {
    void *context;
    int (*read)(void *context, flash_partition_id slot, uint32_t offset,
                void *output, uint32_t size);
    int (*program)(void *context, flash_partition_id slot, uint32_t offset,
                   const void *input, uint32_t size);
    int (*erase)(void *context, flash_partition_id slot, uint32_t offset,
                 uint32_t size);
} update_flash_ops;

/** Bind the ops to the Week-3 simulated NOR device (all callbacks return 0 on
   success, non-zero on failure). */
void update_flash_bind(update_flash_ops *ops, flash_device *dev);

typedef struct {
    flash_device *dev;
    update_flash_ops ops;
    meta_flash_ops meta_ops;
    meta_record record; /**< latest persisted record */
    flash_partition_id active;
    flash_partition_id target;
    uint32_t expected;         /**< declared file length including all framing */
    uint32_t payload_expected; /**< payload length excluding header and any signature trailer */
    uint32_t received;         /**< file bytes accepted from YMODEM */
    uint32_t programmed; /**< bytes actually programmed (4 byte aligned) */
    uint8_t stage[FLASH_PROGRAM_SIZE];
    uint32_t staged;
    int begun;
    int finished;
    int failed;
} update_context;

/** Read the metadata and select the non-active target. No flash is erased.
   ops/meta_ops may be NULL to use the production adapters over dev. */
int update_prepare(update_context *ctx, flash_device *dev,
                   const update_flash_ops *ops, const meta_flash_ops *meta_ops);

/** Fill a ymodem_sink whose callbacks drive this update context. */
void update_sink_init(update_context *ctx, ymodem_sink *sink);

/** Re-read READY and revalidate the candidate before the atomic active-slot
 * commit. On any error the caller must re-read metadata: a marker write may
 * have completed even if its final read-back failed. Never infer selection
 * from this function's return code or an old context. */
int update_activate(update_context *ctx);
#endif
