/** @file
 * @brief Checked configuration changes and ordering of trial activation.
 */
#ifndef WEEK7_CONTROL_H
#define WEEK7_CONTROL_H
#include "boot_config.h"
#include "update.h"
/** Decimal only, surrounding ASCII spaces allowed. Never accepts overflow. */
int config_parse_u32(const char *text, uint32_t *value);
/** Publish runtime parameters only after durable commit succeeds. */
int config_set(meta_flash_ops *ops, cfg_record *cfg, int window, uint32_t value);
/** Persist the trial intent BEFORE publishing the new active slot. */
int trial_activate(update_context *update, meta_flash_ops *ops, cfg_record *cfg);
#endif
