#ifndef WEEK3_UART_H
#define WEEK3_UART_H
#include <stdint.h>
#define UART_RX_CAPACITY 128u
int uart_init(uint32_t clock_hz, uint32_t baud);
int uart_putc(char ch);
int uart_write(const char *text);
int uart_flush(void);
/* 1: byte received, 0: queue empty, -1: invalid output pointer. */
int uart_getc(uint8_t *byte);
void UART0_RX_Handler(void);
extern volatile uint32_t uart_rx_count, uart_rx_dropped, uart_rx_overruns;
#endif
