/* Week 5: single-file YMODEM -> inactive slot -> READY -> active commit.
 * Only persisted metadata selects the boot slot. No automatic fallback (W6).
 * 'u' uploads, 'b' boots now, 'r' explicitly recovers damaged metadata and
 * uploads. With valid metadata the normal command window is 1500 ms.
 */
#include "image.h"
#include "uart.h"
#include "board.h"
#include "update.h"
#include "ymodem.h"
#include "timebase.h"
#define MMIO(addr) (*(volatile uint32_t *)(addr))
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
    for (unsigned i=0; i<8; ++i) text[2+i]=digits[(value>>(28-4*i))&15];
    print(text);
}
extern void jump_to_app(uint32_t msp, uint32_t reset) __attribute__((noreturn));
__attribute__((noinline,noreturn)) void boot_handoff(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
    MMIO(0x40004008u) = 0;
    MMIO(0x4000400cu) = 0xf;
    MMIO(0x40004004u) = 0xc;
    MMIO(0xe000e010u) = 0;
    MMIO(0xe000e014u) = 0;
    MMIO(0xe000e018u) = 0;
    MMIO(0xe000e180u) = UINT32_MAX;
    MMIO(0xe000e280u) = UINT32_MAX;
    MMIO(0xe000ed04u) = (1u<<25) | (1u<<27);
    MMIO(0xe000ed08u) = APP_EXEC_BASE;
    __asm__ volatile ("dsb\nisb" ::: "memory");
    jump_to_app(boot_image.msp, boot_image.reset);
}

static int uart_ymodem_read(void *context, uint8_t *buffer, uint32_t size) {
    (void)context; (void)size;
    /* Any loss invalidates the session; never ACK silently truncated input. */
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
    }
    return 0;
}
static uint32_t uart_ymodem_ms(void *context) {
    (void)context; return timebase_ms();
}

/* On error return to the command loop; do not silently search another slot. */
static int try_boot(flash_device *storage, meta_flash_ops *ops) {
    meta_record record;
    int result = meta_read(ops, &record);
    if (result != META_OK) {
        print("RECOVERY: META "); hex((uint32_t)result); return 0;
    }
    result = image_validate_slot(storage, (flash_partition_id)record.active_slot, &boot_image);
    if (result != IMAGE_OK) {
        print("RECOVERY: IMAGE "); hex((uint32_t)result); return 0;
    }
    print("IMAGE: SLOT "); print(record.active_slot == FLASH_APP ? "A" : "B");
    print(" CRC32 PASS\r\nIMAGE: VERSION "); hex(boot_image.version);
    result = image_load_slot(storage, (flash_partition_id)record.active_slot, &boot_image);
    if (!result) result = boot_verify_loaded(&boot_image);
    if (result) { print("RECOVERY: LOAD "); hex((uint32_t)result); return 0; }
    print("LOAD: verified at 0x00010000\r\nJUMP: application\r\n");
    if (uart_flush()) { boot_status = 0x101u; boot_idle(); }
    boot_status = 0;
    boot_handoff();
}

/* Explicit recovery only. Never guesses an active slot during normal boot.
 * Prefer valid A, else valid B, else reserve A and receive into B. No firmware
 * slot is erased by resetting metadata. A torn reset returns to recovery. */
static int recover_metadata(flash_device *storage, meta_flash_ops *ops) {
    meta_record current;
    int status = meta_read(ops, &current);
    if (status == META_OK) return META_OK;
    if (status != META_RECOVERY_REQUIRED && status != META_AMBIGUOUS &&
        status != META_NO_RECORD) return status;
    image_info info;
    meta_record fresh;
    fresh.active_slot = FLASH_APP;
    if (image_validate_slot(storage, FLASH_APP, &info) != IMAGE_OK &&
        image_validate_slot(storage, FLASH_BACKUP, &info) == IMAGE_OK)
        fresh.active_slot = FLASH_BACKUP;
    fresh.target_slot = META_SLOT_NONE;
    fresh.state = META_STATE_IDLE;
    fresh.progress = fresh.expected_length = fresh.candidate_version = fresh.error = 0;
    for (unsigned page = 0; page < META_PAGE_COUNT; ++page)
        if (ops->erase(ops->context, page * META_PAGE_SIZE, META_PAGE_SIZE)) return META_IO;
    return meta_commit(ops, &fresh);
}

static void upgrade_session(flash_device *storage) {
    update_context update;
    int status = update_prepare(&update, storage, 0, 0);
    if (status != UPDATE_OK) { print("UPGRADE: META "); hex((uint32_t)status); return; }
    /* Drop/overrun before entry also invalidates transfer. Re-init here drains
       commands and clears the counters before emitting the first 'C'. */
    if (uart_init(board_clock_hz(), 115200u)) { boot_status = 0x101u; boot_idle(); }
    ymodem_sink sink;
    update_sink_init(&update, &sink);
    ymodem_io io = {0, uart_ymodem_read, uart_ymodem_write, uart_ymodem_ms};
    ymodem_config config = {APP_EXEC_SIZE, 1000u, 8u, 15000u};
    ymodem_stats stats;
    int result = ymodem_receive(&io, &sink, &config, &stats);
    if (result == YMODEM_OK && update.finished) {
        print("UPGRADE: READY\r\n");
#ifdef WEEK5_UPGRADE_ONLY
        /* Retained test target for inspecting the pre-activation boundary. */
        return;
#else
        status = update_activate(&update);
        if (status == UPDATE_OK) print("UPGRADE: COMMITTED\r\n");
        else { print("UPGRADE: COMMIT ERROR "); hex((uint32_t)status); }
#endif
    } else { print("UPGRADE: FAIL "); hex((uint32_t)(result ? result : YMODEM_PROTOCOL)); }
}

int main(void) {
    flash_device storage = {0};
    board_init();
    if (uart_init(board_clock_hz(), 115200u)) { boot_status = 0x101u; boot_idle(); }
    timebase_init();
    if (flash_sim_bind(&storage, (volatile uint8_t *)FLASH_SIM_BASE, FLASH_SIZE)) {
        boot_status = IMAGE_IO; boot_idle();
    }
    meta_flash_ops ops;
    meta_flash_bind(&ops, &storage);
    meta_record record;
    int status = meta_init(&ops, &record);
    if (status != META_OK) { print("RECOVERY: META "); hex((uint32_t)status); }
    print("BOOT5: command window\r\n");
    uint32_t start = timebase_ms();
    int recovery = status != META_OK;
    for (;;) {
        uint8_t byte;
        if (uart_getc(&byte) == 1) {
            if (byte == 'r') {
                status = recover_metadata(&storage, &ops);
                if (status != META_OK) { print("RECOVERY: META "); hex((uint32_t)status); }
                else { upgrade_session(&storage); recovery = !try_boot(&storage, &ops); }
            } else if (byte == 'u' && !recovery) {
                upgrade_session(&storage);
                /* Always read durable state, including after an uncertain IO. */
                recovery = !try_boot(&storage, &ops);
            } else if (byte == 'u' && recovery) {
                /* An invalid active image still permits uploading if metadata
                   is trustworthy; damaged metadata requires explicit 'r'. */
                if (meta_read(&ops, &record) == META_OK) {
                    upgrade_session(&storage); recovery = !try_boot(&storage, &ops);
                } else print("RECOVERY: send r to reset metadata\r\n");
            } else if (byte == 'b') recovery = !try_boot(&storage, &ops);
        }
        if (!recovery && (uint32_t)(timebase_ms() - start) >= 1500u)
            recovery = !try_boot(&storage, &ops);
    }
}
