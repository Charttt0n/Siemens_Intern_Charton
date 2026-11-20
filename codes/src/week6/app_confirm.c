#include "app_confirm.h"
#include "boot_config.h"
#include "watchdog.h"

int week6_app_confirm(void) {
    flash_device storage = {0};
    meta_flash_ops ops;
    cfg_record record;
    if (flash_sim_bind(&storage, (volatile uint8_t *)FLASH_SIM_BASE, FLASH_SIZE))
        return 1;
    meta_flash_bind(&ops, &storage);
    if (cfg_read(&ops, &record) != CFG_OK) return 1;
    if (record.trial_pending) {
        record.last_confirmed_slot = record.trial_slot;
        record.trial_pending = 0;
        record.trial_stage = CFG_TRIAL_NONE;
        record.trial_slot = META_SLOT_NONE;
        if (cfg_commit(&ops, &record) != CFG_OK) return 1;
    }
    watchdog_stop();
    return 0;
}
