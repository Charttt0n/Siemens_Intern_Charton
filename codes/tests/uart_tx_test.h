#ifndef UART_TX_TEST_H
#define UART_TX_TEST_H
#include <stdint.h>
extern volatile uint32_t uart_tx_test_full, uart_tx_test_reads, uart_tx_test_data;
static inline uint32_t uart_tx_test_read_state(void) {
    ++uart_tx_test_reads;
    return uart_tx_test_full;
}
#endif
