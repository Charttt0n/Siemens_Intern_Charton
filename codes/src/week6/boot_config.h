#ifndef WEEK6_BOOT_CONFIG_H
#define WEEK6_BOOT_CONFIG_H
#include "boot_meta.h"

/* Week-6 boot parameters and trial-boot state, stored as two 4 KiB pages at
 * offsets 0x2000 and 0x3000 of FLASH_PARAMS. Pages 0 and 1 remain Week-5
 * upgrade metadata and are not overwritten here. */
#define CFG_MAGIC 0x4643364du   /* little-endian "M6CF" */
#define CFG_SCHEMA 1u
#define CFG_MARKER 0x54494d43u  /* "CMIT" */
#define CFG_RECORD_SIZE 56u
#define CFG_PAGE_BASE (META_PAGE_COUNT * META_PAGE_SIZE)
#define CFG_DEFAULT_WINDOW_MS 2500u
#define CFG_MIN_WINDOW_MS 200u
#define CFG_MAX_WINDOW_MS 60000u

typedef enum {
    CFG_OK = 0,
    CFG_IO = 1,
    CFG_NO_RECORD = 2,
    CFG_ARGUMENT = 4,
    CFG_VERIFY = 5,
    CFG_AMBIGUOUS = 6
} cfg_result;

typedef struct {
    uint32_t magic, schema, generation;
    uint32_t window_ms;
    uint32_t auto_boot;
    uint32_t trial_slot;
    uint32_t last_confirmed_slot;
    uint32_t trial_pending;
    uint32_t trial_stage;     /* 0 none, 1 armed this session, 2 launched */
    uint32_t reserved[3];
    uint32_t crc, marker;
} cfg_record;

#define CFG_TRIAL_NONE 0u
#define CFG_TRIAL_ARMED 1u
#define CFG_TRIAL_LAUNCHED 2u

void cfg_defaults(cfg_record *record);
int cfg_init(const meta_flash_ops *ops, cfg_record *record);
int cfg_read(const meta_flash_ops *ops, cfg_record *record);
int cfg_commit(const meta_flash_ops *ops, cfg_record *record);
#endif
