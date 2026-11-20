/** @file
 * @brief AN500 board clock and early initialization.
 */
#ifndef WEEK2_BOARD_H
#define WEEK2_BOARD_H
#include <stdint.h>
/** Perform board-specific early initialization before starting the console. */
void board_init(void);
/** Return the AN500 clock frequency in Hz. */
uint32_t board_clock_hz(void);
#endif
