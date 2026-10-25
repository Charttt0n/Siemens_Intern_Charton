#include <stdint.h>
/* Only linked into explicitly instrumented test targets. */
volatile uint32_t uart_tx_test_full = 1u;
volatile uint32_t uart_tx_test_reads;
volatile uint32_t uart_tx_test_data = 0x55u;
