#include <stdint.h>
#include "board.h"
#include "uart.h"

/* Multiple words exercise the startup loops, rather than a single scalar. */
volatile uint32_t runtime_data[5] = {
    0x12345678u, 0x89abcdefu, 0x13579bdfu, 0x2468ace0u, 0xa55aa55au
};
volatile uint32_t runtime_bss[7];
/* 0: booting; 1: pass; 2: C runtime failure; 3: UART failure; 4: CPU failure. */
volatile uint32_t boot_status;
volatile uint32_t exception_number;

__attribute__((noreturn, noinline)) void boot_idle(void) {
    for (;;) __asm__ volatile ("wfi");
}

__attribute__((noreturn, noinline)) void fault_idle(void) {
    for (;;) __asm__ volatile ("wfi");
}

__attribute__((noreturn)) void exception_handler(uint32_t number) {
    exception_number = number;
    fault_idle();
}

/* Fixed-width hexadecimal output avoids printf and heap dependencies. */
static int log_hex(const char *label, uint32_t value) {
    char text[11] = "0x00000000";
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i)
        text[2 + i] = digits[(value >> (28u - 4u * i)) & 15u];
    return uart_write(label) || uart_write(text) || uart_write("\r\n");
}

static int runtime_ok(void) {
    const uint32_t expected[5] = {
        0x12345678u, 0x89abcdefu, 0x13579bdfu, 0x2468ace0u, 0xa55aa55au
    };
    for (unsigned i = 0; i < 5; ++i)
        if (runtime_data[i] != expected[i]) return 0;
    for (unsigned i = 0; i < 7; ++i)
        if (runtime_bss[i] != 0u) return 0;
    return 1;
}

int main(void) {
    board_init();
    if (uart_init(board_clock_hz(), 115200u) != 0) {
        boot_status = 3u;
        boot_idle();
    }
    const uint32_t part = (*(volatile uint32_t *)0xE000ED00u >> 4) & 0xfffu;
    int crt_ok = runtime_ok();
    boot_status = part != 0xc27u ? 4u : (crt_ok ? 1u : 2u);
    if (uart_write("BOOT: week2 mps2-an500 Cortex-M7\r\n") != 0 ||
        uart_write("CLOCK: 25000000 Hz (QEMU fixed)\r\n") != 0 ||
        uart_write("UART0: 115200 requested, divisor=217, 8N1\r\n") != 0 ||
        uart_write("[INIT] board ready; UART TX enabled; semihosting off\r\n") != 0 ||
        log_hex("[CPU] PartNo=", part) != 0 ||
        log_hex("[CRT] data[4]=", runtime_data[4]) != 0 ||
        log_hex("[CRT] bss[6]=", runtime_bss[6]) != 0 ||
        uart_write(crt_ok ? "[CHECK] data[5] and bss[7]: PASS\r\n" :
                           "[CHECK] data[5] or bss[7]: FAIL\r\n") != 0 ||
        log_hex("[STATE] boot_status=", boot_status) != 0 ||
        uart_write("[NEXT] enter boot_idle (WFI)\r\n") != 0 ||
        uart_write(boot_status == 1u ? "PASS: C runtime; UART boot ready\r\n" :
                   "FAIL: CPU or C runtime\r\n") != 0 || uart_flush() != 0) {
        boot_status = 3u;
    }
    boot_idle();
}
