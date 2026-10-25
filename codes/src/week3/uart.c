#include "uart.h"
#include "irq.h"
#define REG(offset) (*(volatile uint32_t *)(0x40004000u + (offset)))
#define DATA REG(0x00u)
#define STATE REG(0x04u)
#define CTRL REG(0x08u)
#define INTSTATUS REG(0x0cu)
#define BAUDDIV REG(0x10u)
#ifdef UART_TX_FAULT_TEST
#include "uart_tx_test.h"
#undef DATA
#define DATA uart_tx_test_data
#define TX_STATE() uart_tx_test_read_state()
#else
#define TX_STATE() STATE
#endif
#define NVIC_ISER (*(volatile uint32_t *)0xe000e100u)
#define NVIC_ICER (*(volatile uint32_t *)0xe000e180u)
#define NVIC_ICPR (*(volatile uint32_t *)0xe000e280u)

static volatile uint8_t rx[UART_RX_CAPACITY];
static volatile uint32_t head, tail;
volatile uint32_t uart_rx_count, uart_rx_dropped, uart_rx_overruns;

int uart_init(uint32_t clock_hz, uint32_t baud) {
    if (!baud || !clock_hz) return -1;
    uint32_t divisor = clock_hz / baud;
    if (clock_hz % baud >= baud - baud / 2u) ++divisor;
    if (divisor < 16u || divisor > 0xfffffu) return -1;
    uint32_t saved = irq_save();
    NVIC_ICER = 1u;
    CTRL = 0u;
    head = tail = 0u;
    uart_rx_count = uart_rx_dropped = uart_rx_overruns = 0u;
    BAUDDIV = divisor;
    STATE = 0xcu;
    INTSTATUS = 0xfu;
    CTRL = 3u;
    if (STATE & 2u) (void)DATA; /* Drain stale receive data on re-init. */
    NVIC_ICPR = 1u;
    CTRL = 0xbu; /* TX + RX + RX interrupt; TX remains bounded polling. */
    NVIC_ISER = 1u; /* AN500 UART0 RX = external IRQ 0. */
    __asm__ volatile ("dsb\nisb" ::: "memory");
    irq_restore(saved);
    return 0;
}

void UART0_RX_Handler(void) {
    /* Clear before reading DATA: the read can accept the next host byte and
       assert a fresh interrupt, which must not be cleared accidentally. */
    INTSTATUS = 2u;
    if (STATE & 8u) { ++uart_rx_overruns; STATE = 8u; }
    if (STATE & 2u) {
        uint8_t byte = (uint8_t)DATA;
        uint32_t next = (head + 1u) % UART_RX_CAPACITY;
        ++uart_rx_count;
        if (next == tail) ++uart_rx_dropped;
        else { rx[head] = byte; head = next; }
    }
}

int uart_getc(uint8_t *byte) {
    if (!byte) return -1;
    uint32_t saved = irq_save();
    int available = head != tail;
    if (available) { *byte = rx[tail]; tail = (tail + 1u) % UART_RX_CAPACITY; }
    irq_restore(saved);
    return available;
}

int uart_flush(void) {
    if (!(CTRL & 1u)) return -1;
    for (uint32_t n = 0; n < 1000000u; ++n)
        if (!(TX_STATE() & 1u)) return 0;
    return -1;
}
int uart_putc(char ch) {
    if (uart_flush()) return -1;
    DATA = (uint8_t)ch;
    return 0;
}
int uart_write(const char *text) {
    if (!text) return -1;
    while (*text) if (uart_putc(*text++)) return -1;
    return 0;
}
