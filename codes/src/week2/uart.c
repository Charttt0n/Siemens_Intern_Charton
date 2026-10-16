#include "uart.h"

#define REG(offset) (*(volatile uint32_t *)(0x40004000u + (offset)))
#define DATA REG(0x00u)
#define STATE REG(0x04u)
#define CTRL REG(0x08u)
#define INTSTATUS REG(0x0cu)
#define BAUDDIV REG(0x10u)
#define TX_ENABLE 1u
#define TX_FULL 1u
#define POLL_LIMIT 1000000u

int uart_init(uint32_t clock_hz, uint32_t baud) {
    if (baud == 0u || clock_hz == 0u) return -1;
    /* Round to nearest divisor without overflow in clock_hz + baud/2. */
    uint32_t divisor = clock_hz / baud;
    uint32_t remainder = clock_hz % baud;
    if (remainder >= baud - baud / 2u) ++divisor;
    if (divisor < 16u || divisor > 0xfffffu) return -1;
    CTRL = 0u;
    BAUDDIV = divisor;
    STATE = 0xcu;       /* TX/RX overrun flags: write one to clear. */
    INTSTATUS = 0xfu;
    CTRL = TX_ENABLE;   /* Week 2: TX only, no interrupt enable bits. */
    return 0;
}

int uart_flush(void) {
    if ((CTRL & TX_ENABLE) == 0u) return -1;
    for (uint32_t n = 0; n < POLL_LIMIT; ++n) {
        if ((STATE & TX_FULL) == 0u) return 0;
    }
    return -1;
}

int uart_putc(char ch) {
    if (uart_flush() != 0) return -1;
    DATA = (uint8_t)ch;
    return 0;
}

int uart_write(const char *text) {
    if (!text) return -1;
    while (*text) {
        if (uart_putc(*text++) != 0) return -1;
    }
    return 0;
}
