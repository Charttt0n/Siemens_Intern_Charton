#ifndef WEEK6_WATCHDOG_H
#define WEEK6_WATCHDOG_H
#include <stdint.h>

/* Product driver for the QEMU mps2-an500 CMSDK APB watchdog at 0x40008000.
 * The clock is the 25 MHz system clock. This is not a host-side test hook. */
#define WATCHDOG_BASE 0x40008000u
#define WATCHDOG_CLK_HZ 25000000u

void watchdog_start(uint32_t timeout_ms);
void watchdog_kick(void);
void watchdog_stop(void);
int watchdog_running(void);
#endif
