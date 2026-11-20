#include "control.h"

int config_parse_u32(const char *text, uint32_t *value) {
    uint32_t n = 0;
    int digits = 0;
    if (!text || !value) return 0;
    while (*text == ' ') ++text;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text++ - '0');
        if (n > (UINT32_MAX - digit) / 10u) return 0;
        n = n * 10u + digit;
        digits = 1;
    }
    while (*text == ' ') ++text;
    if (!digits || *text) return 0;
    *value = n;
    return 1;
}

int config_set(meta_flash_ops *ops, cfg_record *cfg, int window, uint32_t value) {
    cfg_record candidate;
    int result;
    if (!ops || !cfg) return CFG_ARGUMENT;
    candidate = *cfg;
    if (window) candidate.window_ms = value;
    else candidate.auto_boot = value;
    result = cfg_commit(ops, &candidate);
    if (result == CFG_OK) *cfg = candidate;
    return result;
}

int trial_activate(update_context *update, meta_flash_ops *ops, cfg_record *cfg) {
    cfg_record candidate;
    int result;
    if (!update || !ops || !cfg || !update->finished || update->failed)
        return UPDATE_STATE;
    candidate = *cfg;
    candidate.trial_pending = 1;
    candidate.trial_slot = update->target;
    candidate.trial_stage = CFG_TRIAL_ARMED;
    candidate.last_confirmed_slot = update->active;
    if (cfg_commit(ops, &candidate) != CFG_OK) return UPDATE_META;
    *cfg = candidate;
    result = update_activate(update);
    /* If activation is interrupted, intent remains for the inactive slot.
       The old app must not confirm that intent (see week7/app_confirm.c).
       If the marker landed, a restart sees ARMED and uses the watchdog. */
    return result;
}
