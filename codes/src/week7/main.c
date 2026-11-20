/* Week 7: signed upgrade, fail-safe rollback, CMSDK watchdog, config CLI.
 * Week-5 metadata and 32-byte image headers are reused without change. */
#include "image.h"
#include "uart.h"
#include "board.h"
#include "update.h"
#include "ymodem.h"
#include "timebase.h"
#include "secure_image.h"
#include "boot_config.h"
#include "watchdog.h"
#include "control.h"
#include <string.h>

#define MMIO(addr) (*(volatile uint32_t *)(addr))
#define BL_VERSION "week7-1"
#define WDT_SERVICE_MS 30000u
#define WDT_TRIAL_MS 2000u
#define LINE_MAX 48u

volatile uint32_t boot_status, exception_number;
image_info boot_image;

__attribute__((noreturn)) void boot_idle(void) { for (;;) __asm__ volatile ("wfi"); }
__attribute__((noreturn)) void exception_handler(uint32_t number) {
    exception_number = number; boot_status = 0x100u; boot_idle();
}
static void print(const char *text) {
    if (uart_write(text)) { boot_status = 0x101u; boot_idle(); }
}
static void hex(uint32_t value) {
    char text[] = "0x00000000\r\n";
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) text[2 + i] = digits[(value >> (28 - 4 * i)) & 15];
    print(text);
}
extern void jump_to_app(uint32_t msp, uint32_t reset) __attribute__((noreturn));

__attribute__((noinline,noreturn)) void boot_handoff(int keep_watchdog) {
    if (!keep_watchdog) watchdog_stop();
    __asm__ volatile ("cpsid i" ::: "memory");
    MMIO(0x40004008u) = 0;
    MMIO(0x4000400cu) = 0xf;
    MMIO(0x40004004u) = 0xc;
    MMIO(0xe000e010u) = 0;
    MMIO(0xe000e014u) = 0;
    MMIO(0xe000e018u) = 0;
    MMIO(0xe000e180u) = UINT32_MAX;
    MMIO(0xe000e280u) = UINT32_MAX;
    MMIO(0xe000ed04u) = (1u << 25) | (1u << 27);
    MMIO(0xe000ed08u) = APP_EXEC_BASE;
    __asm__ volatile ("dsb\nisb" ::: "memory");
    jump_to_app(boot_image.msp, boot_image.reset);
}

static int uart_ymodem_read(void *context, uint8_t *buffer, uint32_t size) {
    (void)context; (void)size;
    watchdog_kick();
    if (uart_rx_dropped || uart_rx_overruns || (MMIO(0x40004004u) & 8u)) return -1;
    return uart_getc(buffer);
}
static int uart_ymodem_write(void *context, const uint8_t *data, uint32_t size) {
    (void)context;
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t start = timebase_ms();
        while (MMIO(0x40004004u) & 1u)
            if ((uint32_t)(timebase_ms() - start) >= 250u) return -1;
        if (!(MMIO(0x40004008u) & 1u)) return -1;
        MMIO(0x40004000u) = data[i];
        watchdog_kick();
    }
    return 0;
}
static uint32_t uart_ymodem_ms(void *context) {
    (void)context; return timebase_ms();
}

static flash_partition_id other_slot(flash_partition_id slot) {
    return slot == FLASH_APP ? FLASH_BACKUP : FLASH_APP;
}
static const char *slot_name(uint32_t slot) {
    return slot == FLASH_APP ? "A" : (slot == FLASH_BACKUP ? "B" : "-");
}

static int persist_active(meta_flash_ops *ops, flash_partition_id active) {
    meta_record record;
    if (meta_read(ops, &record) != META_OK) return 0;
    record.active_slot = active;
    record.target_slot = META_SLOT_NONE;
    record.state = META_STATE_IDLE;
    record.progress = record.expected_length = record.candidate_version = record.error = 0;
    return meta_commit(ops, &record) == META_OK;
}

static int start_from_slot(flash_device *storage, flash_partition_id slot, int trial) {
    int result = image_load_slot(storage, slot, &boot_image);
    if (!result) result = boot_verify_loaded(&boot_image);
    if (result) { print("RECOVERY: LOAD "); hex((uint32_t)result); return 0; }
    print("LOAD: verified at 0x00010000\r\n");
    if (trial) {
        print("TRIAL: LAUNCH\r\nJUMP: application\r\n");
        watchdog_start(WDT_TRIAL_MS);
    } else {
        print("JUMP: application\r\n");
        watchdog_start(WDT_SERVICE_MS);
    }
    if (uart_flush()) { boot_status = 0x101u; boot_idle(); }
    boot_status = 0;
    boot_handoff(trial);
}

static int rollback_to(flash_device *storage, meta_flash_ops *ops, cfg_record *cfg,
                       flash_partition_id slot, const char *why) {
    print("ROLLBACK: "); print(why); print("\r\nROLLBACK: SELECT ");
    print(slot_name(slot)); print("\r\n");
    if (!persist_active(ops, slot)) { print("ROLLBACK: META FAIL\r\n"); return 0; }
    cfg->trial_pending = 0;
    cfg->trial_stage = CFG_TRIAL_NONE;
    cfg->trial_slot = META_SLOT_NONE;
    cfg->last_confirmed_slot = slot;
    if (cfg_commit(ops, cfg) != CFG_OK) { print("ROLLBACK: CFG FAIL\r\n"); return 0; }
    print("ROLLBACK: COMMITTED\r\nIMAGE: SLOT "); print(slot_name(slot));
    print(" SIG PASS\r\nIMAGE: VERSION "); hex(boot_image.version);
    return start_from_slot(storage, slot, 0);
}

/* Returns 1 if the application was launched, 0 if the CLI should continue. */
static int try_boot(flash_device *storage, meta_flash_ops *ops, cfg_record *cfg) {
    meta_record record;
    flash_partition_id active, backup;
    int status, secure;
    watchdog_kick();
    status = meta_read(ops, &record);
    if (status != META_OK) {
        print("RECOVERY: META "); hex((uint32_t)status);
        return 0;
    }
    active = (flash_partition_id)record.active_slot;
    backup = other_slot(active);

    if (cfg->trial_pending && cfg->trial_slot == (uint32_t)active &&
        cfg->trial_stage == CFG_TRIAL_LAUNCHED) {
        if (cfg->last_confirmed_slot != META_SLOT_NONE &&
            cfg->last_confirmed_slot != (uint32_t)active &&
            secure_validate_slot(storage, (flash_partition_id)cfg->last_confirmed_slot,
                                 &boot_image) == SECURE_OK)
            return rollback_to(storage, ops, cfg,
                               (flash_partition_id)cfg->last_confirmed_slot,
                               "TRIAL UNCONFIRMED");
        if (secure_validate_slot(storage, backup, &boot_image) == SECURE_OK)
            return rollback_to(storage, ops, cfg, backup, "TRIAL UNCONFIRMED");
        print("ROLLBACK: TRIAL UNCONFIRMED\r\nROLLBACK: NO VALID BACKUP\r\n");
        return 0;
    }

    secure = secure_validate_slot(storage, active, &boot_image);
    if (secure != SECURE_OK) {
        print("ROLLBACK: ACTIVE INVALID "); hex((uint32_t)secure);
        if (secure_validate_slot(storage, backup, &boot_image) == SECURE_OK)
            return rollback_to(storage, ops, cfg, backup, "ACTIVE INVALID");
        print("ROLLBACK: NO VALID BACKUP\r\n");
        return 0;
    }

    if (cfg->trial_pending && cfg->trial_slot == (uint32_t)active &&
        cfg->trial_stage == CFG_TRIAL_ARMED) {
        cfg->trial_stage = CFG_TRIAL_LAUNCHED;
        if (cfg_commit(ops, cfg) != CFG_OK) {
            print("TRIAL: CFG FAIL\r\n");
            return 0;
        }
        print("IMAGE: SLOT "); print(slot_name(active));
        print(" SIG PASS\r\nIMAGE: VERSION "); hex(boot_image.version);
        return start_from_slot(storage, active, 1);
    }

    print("IMAGE: SLOT "); print(slot_name(active));
    print(" SIG PASS\r\nIMAGE: VERSION "); hex(boot_image.version);
    return start_from_slot(storage, active, 0);
}

static int recover_metadata(flash_device *storage, meta_flash_ops *ops) {
    meta_record current, fresh;
    image_info info;
    int status = meta_read(ops, &current);
    unsigned page;
    if (status == META_OK) return META_OK;
    if (status != META_RECOVERY_REQUIRED && status != META_AMBIGUOUS &&
        status != META_NO_RECORD) return status;
    fresh.active_slot = FLASH_APP;
    if (secure_validate_slot(storage, FLASH_APP, &info) != SECURE_OK &&
        secure_validate_slot(storage, FLASH_BACKUP, &info) == SECURE_OK)
        fresh.active_slot = FLASH_BACKUP;
    fresh.target_slot = META_SLOT_NONE;
    fresh.state = META_STATE_IDLE;
    fresh.progress = fresh.expected_length = fresh.candidate_version = fresh.error = 0;
    for (page = 0; page < META_PAGE_COUNT; ++page)
        if (ops->erase(ops->context, page * META_PAGE_SIZE, META_PAGE_SIZE)) return META_IO;
    return meta_commit(ops, &fresh);
}

static void print_status(flash_device *storage, meta_flash_ops *ops, const cfg_record *cfg) {
    meta_record record;
    image_info info;
    unsigned i;
    flash_partition_id slots[2] = { FLASH_APP, FLASH_BACKUP };
    print("BOOT7: BL " BL_VERSION "\r\n");
    for (i = 0; i < 2; ++i) {
        int result = secure_validate_slot(storage, slots[i], &info);
        print("SLOT "); print(slot_name(slots[i])); print(": ");
        if (result == SECURE_OK) {
            print("VER "); hex(info.version);
            print("SIG OK\r\n");
        } else {
            print("INVALID "); hex((uint32_t)result);
        }
    }
    if (meta_read(ops, &record) == META_OK) {
        print("ACTIVE: "); print(slot_name(record.active_slot)); print("\r\n");
    } else print("ACTIVE: unknown\r\n");
    print("TRIAL: ");
    if (cfg->trial_pending) {
        print(slot_name(cfg->trial_slot));
        print(cfg->trial_stage == CFG_TRIAL_LAUNCHED ? " launched\r\n" : " armed\r\n");
    } else print("none\r\n");
    print("PARAM window_ms=");
    {
        char text[] = "00000 auto_boot=0\r\n";
        uint32_t value = cfg->window_ms;
        for (i = 5; i-- > 0;) { text[i] = (char)('0' + (value % 10u)); value /= 10u; }
        text[16] = (char)('0' + (cfg->auto_boot & 1u));
        print(text);
    }
}

static void print_params(const cfg_record *cfg) {
    print("PARAM window_ms=");
    {
        char text[] = "00000\r\n";
        uint32_t value = cfg->window_ms;
        unsigned i;
        for (i = 5; i-- > 0;) { text[i] = (char)('0' + (value % 10u)); value /= 10u; }
        print(text);
    }
    print("PARAM auto_boot=");
    print(cfg->auto_boot ? "1\r\n" : "0\r\n");
}

static void set_param(meta_flash_ops *ops, cfg_record *cfg, const char *line) {
    int ok = 0;
    uint32_t value = 0;
    int window;
    while (*line == ' ') ++line;
    if (!*line) { print_params(cfg); return; }
    if (!strncmp(line, "window_ms", 9) && (line[9] == ' ' || line[9] == 0)) {
        line += 9;
        while (*line == ' ') ++line;
        if (!*line) { print_params(cfg); return; }
        ok = config_parse_u32(line, &value);
        if (!ok || value < CFG_MIN_WINDOW_MS || value > CFG_MAX_WINDOW_MS) {
            print("CONFIG: BAD window_ms\r\n");
            return;
        }
        window = 1;
    } else if (!strncmp(line, "auto_boot", 9) && (line[9] == ' ' || line[9] == 0)) {
        line += 9;
        while (*line == ' ') ++line;
        if (!*line) { print_params(cfg); return; }
        ok = config_parse_u32(line, &value);
        if (!ok || value > 1u) { print("CONFIG: BAD auto_boot\r\n"); return; }
        window = 0;
    } else {
        print("CONFIG: UNKNOWN PARAM\r\n");
        return;
    }
    if (config_set(ops, cfg, window, value) == CFG_OK) print("CONFIG: SAVED\r\n");
    else print("CONFIG: SAVE FAIL\r\n");
}

static void factory_reset(flash_device *storage, meta_flash_ops *ops, cfg_record *cfg) {
    unsigned page;
    image_info info;
    meta_record fresh;
    (void)storage;
    for (page = 0; page < META_PAGE_COUNT + 2u; ++page)
        if (ops->erase(ops->context, page * META_PAGE_SIZE, META_PAGE_SIZE)) {
            print("FACTORY: ERASE FAIL\r\n");
            return;
        }
    fresh.active_slot = FLASH_APP;
    if (secure_validate_slot(storage, FLASH_APP, &info) != SECURE_OK &&
        secure_validate_slot(storage, FLASH_BACKUP, &info) == SECURE_OK)
        fresh.active_slot = FLASH_BACKUP;
    fresh.target_slot = META_SLOT_NONE;
    fresh.state = META_STATE_IDLE;
    fresh.progress = fresh.expected_length = fresh.candidate_version = fresh.error = 0;
    if (meta_commit(ops, &fresh) != META_OK) { print("FACTORY: META FAIL\r\n"); return; }
    cfg_defaults(cfg);
    if (secure_validate_slot(storage, (flash_partition_id)fresh.active_slot, &info) == SECURE_OK)
        cfg->last_confirmed_slot = fresh.active_slot;
    if (cfg_commit(ops, cfg) != CFG_OK) { print("FACTORY: CFG FAIL\r\n"); return; }
    print("FACTORY: DONE\r\n");
}

static void upgrade_session(flash_device *storage, meta_flash_ops *ops, cfg_record *cfg) {
    update_context update;
    int status = update_prepare(&update, storage, 0, 0);
    if (status != UPDATE_OK) { print("UPGRADE: META "); hex((uint32_t)status); return; }
    if (uart_init(board_clock_hz(), 115200u)) { boot_status = 0x101u; boot_idle(); }
    ymodem_sink sink;
    update_sink_init(&update, &sink);
    ymodem_io io = {0, uart_ymodem_read, uart_ymodem_write, uart_ymodem_ms};
    ymodem_config config = {APP_EXEC_SIZE, 1000u, 8u, 15000u};
    ymodem_stats stats;
    watchdog_start(WDT_SERVICE_MS);
    int result = ymodem_receive(&io, &sink, &config, &stats);
    if (result == YMODEM_OK && update.finished) {
        print("UPGRADE: READY\r\n");
        status = trial_activate(&update, ops, cfg);
        if (status == UPDATE_OK) {
            print("UPGRADE: COMMITTED\r\n");
        } else {
            print("UPGRADE: COMMIT ERROR "); hex((uint32_t)status);
        }
    } else {
        print("UPGRADE: FAIL "); hex((uint32_t)(result ? result : YMODEM_PROTOCOL));
    }
    watchdog_start(WDT_SERVICE_MS);
}

static int recover_and_maybe_boot(flash_device *storage, meta_flash_ops *ops, cfg_record *cfg) {
    int status = recover_metadata(storage, ops);
    if (status != META_OK) { print("RECOVERY: META "); hex((uint32_t)status); return 0; }
    upgrade_session(storage, ops, cfg);
    return try_boot(storage, ops, cfg);
}

static void help(void) {
    print("BOOT7: commands u/upgrade b/boot r/recover v/version|status "
          "p/param f/factory\r\n");
}

static int handle_line(char *line, flash_device *storage, meta_flash_ops *ops,
                       cfg_record *cfg) {
    while (*line == ' ' || *line == '\t') ++line;
    if (!*line) return 0;
    if (!strcmp(line, "u") || !strcmp(line, "upgrade")) {
        upgrade_session(storage, ops, cfg);
        return try_boot(storage, ops, cfg);
    }
    if (!strcmp(line, "b") || !strcmp(line, "boot"))
        return try_boot(storage, ops, cfg);
    if (!strcmp(line, "r") || !strcmp(line, "recover"))
        return recover_and_maybe_boot(storage, ops, cfg);
    if (!strcmp(line, "v") || !strcmp(line, "version") || !strcmp(line, "status")) {
        print_status(storage, ops, cfg);
        return 0;
    }
    if (!strcmp(line, "p") || !strcmp(line, "param") || !strncmp(line, "p ", 2) ||
        !strncmp(line, "param ", 6)) {
        const char *args = line;
        if (!strncmp(line, "param", 5)) args = line + 5;
        else if (line[0] == 'p') args = line + 1;
        set_param(ops, cfg, args);
        return 0;
    }
    if (!strcmp(line, "f") || !strcmp(line, "factory")) {
        print("FACTORY: send YES to confirm\r\n");
        return 0;
    }
    if (!strcmp(line, "h") || !strcmp(line, "help")) { help(); return 0; }
    print("BOOT7: unknown command\r\n");
    return 0;
}

int main(void) {
    flash_device storage = {0};
    meta_flash_ops ops;
    meta_record meta;
    cfg_record cfg;
    char line[LINE_MAX];
    uint32_t length = 0, start, last_byte = 0;
    int status, recovery, launched = 0, cfg_ok;
    uint8_t byte, factory_armed = 0, overflow = 0;

    board_init();
    if (uart_init(board_clock_hz(), 115200u)) { boot_status = 0x101u; boot_idle(); }
    timebase_init();
    watchdog_start(WDT_SERVICE_MS);
    if (flash_sim_bind(&storage, (volatile uint8_t *)FLASH_SIM_BASE, FLASH_SIZE)) {
        boot_status = IMAGE_IO; boot_idle();
    }
    if (secure_init() != SECURE_OK) { print("SIG: INIT FAIL\r\n"); boot_idle(); }
    meta_flash_bind(&ops, &storage);
    status = meta_init(&ops, &meta);
    if (status != META_OK) { print("RECOVERY: META "); hex((uint32_t)status); }
    cfg_ok = cfg_init(&ops, &cfg) == CFG_OK;
    if (!cfg_ok) {
        cfg_defaults(&cfg);
        print("RECOVERY: CFG\r\n");
    }
    print("BOOT7: command window\r\n");
    start = timebase_ms();
    recovery = status != META_OK || !cfg_ok;
    for (;;) {
        watchdog_kick();
        if (uart_getc(&byte) == 1) {
            last_byte = timebase_ms();
            if (byte == '\r' || byte == '\n') {
                if (overflow) {
                    print("CONFIG: LINE TOO LONG\r\n");
                    overflow = 0; length = 0; factory_armed = 0;
                    continue;
                }
                if (length) {
                    line[length] = 0;
                    if (!strcmp(line, "f") || !strcmp(line, "factory"))
                        factory_armed = 1;
                    if (factory_armed && !strcmp(line, "YES")) {
                        factory_reset(&storage, &ops, &cfg);
                        cfg_ok = cfg_read(&ops, &cfg) == CFG_OK;
                        recovery = !cfg_ok || meta_read(&ops, &meta) != META_OK;
                        factory_armed = 0;
                        length = 0;
                        start = timebase_ms();
                        continue;
                    }
                    if (factory_armed && strcmp(line, "f") && strcmp(line, "factory"))
                        factory_armed = 0;
                    /* Corrupt trial state must not silently disable rollback. */
                    if (!cfg_ok && strcmp(line, "f") && strcmp(line, "factory") &&
                        strcmp(line, "v") && strcmp(line, "version") && strcmp(line, "status") &&
                        strcmp(line, "h") && strcmp(line, "help"))
                        print("RECOVERY: CFG RESET REQUIRED\r\n");
                    else launched = handle_line(line, &storage, &ops, &cfg);
                    length = 0;
                    start = timebase_ms();
                }
            } else if (byte >= 32 && byte < 127) {
                if (length + 1 < LINE_MAX && !overflow) line[length++] = (char)byte;
                else overflow = 1;
            }
        }
        /* Lone shortcuts are accepted after 250 ms idle; complete command
           lines can arrive in the same UART burst without prefix execution. */
        if (length == 1 && !overflow &&
            (uint32_t)(timebase_ms() - last_byte) >= 250u &&
            (line[0] == 'u' || line[0] == 'b' || line[0] == 'r' ||
             line[0] == 'v' || line[0] == 'h')) {
            line[1] = 0;
            factory_armed = 0;
            if (cfg_ok || line[0] == 'v' || line[0] == 'h')
                launched = handle_line(line, &storage, &ops, &cfg);
            else print("RECOVERY: CFG RESET REQUIRED\r\n");
            length = 0;
            start = timebase_ms();
        }
        if (launched) recovery = 1; /* handoff does not return */
        if (!recovery && !length && !overflow && cfg.auto_boot &&
            (uint32_t)(timebase_ms() - start) >= cfg.window_ms) {
            recovery = !try_boot(&storage, &ops, &cfg);
            start = timebase_ms();
        }
    }
}
