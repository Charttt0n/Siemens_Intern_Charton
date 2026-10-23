#include <stdint.h>
#include "uart.h"
#define REG(addr) (*(volatile uint32_t *)(addr))
volatile uint32_t app_data[3] = {0x12345678u, 0xa55aa55au, 0x11223344u};
volatile uint32_t app_bss[5];
volatile uint32_t app_status, app_svc_count, app_exception;
volatile uint32_t app_handoff_ok;
__attribute__((noreturn)) void app_idle(void) { for (;;) __asm__ volatile ("wfi"); }
void App_SVC_Handler(void) { ++app_svc_count; }
__attribute__((noreturn)) void app_exception_handler(uint32_t number) {
    app_exception = number; app_status = 3; app_idle();
}
static void print(const char *text) {
    if (uart_write(text)) { app_status = 4; app_idle(); }
}
int app_main(void) {
    uint32_t primask, basepri, control, faultmask;
    __asm__ volatile ("mrs %0, primask\nmrs %1, basepri\nmrs %2, control\nmrs %3, faultmask"
                      : "=r"(primask), "=r"(basepri), "=r"(control), "=r"(faultmask));
    app_handoff_ok = primask == 1 && !basepri && !control && !faultmask &&
        REG(0xe000ed08u) == 0x10000 && !REG(0xe000e100u) && !REG(0xe000e200u) &&
        !(REG(0xe000ed04u) & ((1u<<26)|(1u<<28))) && !(REG(0xe000e010u)&7u) &&
        !REG(0x40004008u);
    int crt_ok = app_data[0] == 0x12345678u && app_data[1] == 0xa55aa55au &&
                 app_data[2] == 0x11223344u;
    for (unsigned i=0; i<5; ++i) if (app_bss[i]) crt_ok=0;
    if (uart_init(25000000u, 115200u)) { app_status=4; app_idle(); }
    if (!crt_ok || !app_handoff_ok) {
        app_status=2; print("APP: CRT or handoff FAIL\r\n> "); app_idle();
    }
    print("APP: CRT PASS\r\nAPP: HANDOFF PASS\r\n");
    __asm__ volatile ("cpsie i\nsvc #0" ::: "memory");
    if (app_svc_count != 1) { app_status=2; print("APP: SVC FAIL\r\n> "); app_idle(); }
    print("APP: SVC PASS\r\nAPP: RUNNING\r\n> ");
    app_status=1; app_idle();
}
