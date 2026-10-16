#ifndef WEEK2_UART_H
#define WEEK2_UART_H
#include <stdint.h>
/* 0 = success; -1 = invalid configuration, disabled TX or polling limit.
   Polling limit is an iteration bound, not a calibrated time interval. */
int uart_init(uint32_t clock_hz, uint32_t baud);
int uart_putc(char ch);
int uart_write(const char *text);
int uart_flush(void);
#endif
