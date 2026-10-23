#include "flash.h"
#include "uart.h"
#include "board.h"

static flash_device storage;
volatile uint32_t boot_status, exception_number;
#ifdef WEEK3_TEST
volatile uint32_t test_pause_rx;
#endif
__attribute__((noreturn)) void fault_idle(void) {
    for (;;) __asm__ volatile ("wfi");
}
__attribute__((noreturn)) void exception_handler(uint32_t number) {
    exception_number = number;
    boot_status = 4u;
    fault_idle();
}
static void print(const char *text) {
    if (uart_write(text)) { boot_status = 3u; fault_idle(); }
}
static void hex(uint32_t value) {
    char text[] = "0x00000000\r\n";
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) text[2+i] = digits[(value >> (28-4*i)) & 15];
    print(text);
}
static void result(int status) {
    print(status == FLASH_OK ? "OK\r\n" : "ERROR ");
    if (status) hex((uint32_t)status);
}

/* A deliberately small diagnostics interface. Only the first parameter erase
   block can be modified here; image download and CRC belong to later weeks. */
__attribute__((noinline)) void command(uint8_t byte) {
    uint8_t data[4];
    const uint8_t marker[4] = {'W', '3', 'O', 'K'};
    switch (byte) {
    case 'p': print("PONG\r\n"); break;
    case 'r':
        if (flash_read(&storage, FLASH_PARAMS, 0u, data, sizeof(data))) {
            print("ERROR read\r\n"); break;
        }
        print("PARAM ");
        hex((uint32_t)data[0] | (uint32_t)data[1] << 8 |
            (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24);
        break;
    case 'e': result(flash_erase(&storage, FLASH_PARAMS, 0, FLASH_ERASE_SIZE)); break;
    case 'w': result(flash_program(&storage, FLASH_PARAMS, 0, marker, sizeof(marker))); break;
    case 's':
        print("RX "); hex(uart_rx_count);
        print("DROP "); hex(uart_rx_dropped);
        print("OVERRUN "); hex(uart_rx_overruns); break;
    case 'h': print("p=ping r=read e=erase-param-sector w=write-marker s=stats h=help\r\n"); break;
    default: print("ERROR command\r\n"); break;
    }
    print("> ");
}

int main(void) {
    board_init();
    if (uart_init(board_clock_hz(), 115200u)) { boot_status = 3u; fault_idle(); }
    if (flash_sim_bind(&storage, (volatile uint8_t *)FLASH_SIM_BASE, FLASH_SIZE)) {
        boot_status = 2u; fault_idle();
    }
#ifdef WEEK3_TEST
    extern int flash_selftest(volatile uint8_t *memory);
    int failed_line = flash_selftest((volatile uint8_t *)FLASH_SIM_BASE);
    if (failed_line) {
        print("SELFTEST FAIL "); hex((uint32_t)failed_line);
        boot_status = 2u; fault_idle();
    }
    print("SELFTEST PASS\r\n");
#endif
    boot_status = 1u;
    print("BOOT: week3 UART RX IRQ; simulated NOR; host checkpoints\r\n> ");
    for (;;) {
        uint8_t byte;
#ifdef WEEK3_TEST
        if (test_pause_rx) continue;
#endif
        /* Foreground polling avoids the check-then-WFI lost-wakeup race.
           Device DATA is only read in the interrupt handler. */
        if (uart_getc(&byte) == 1 && byte != '\r' && byte != '\n') command(byte);
    }
}
