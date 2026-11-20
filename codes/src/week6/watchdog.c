#include "watchdog.h"
#ifndef WEEK6_HOST_TEST

#define REG(offset) (*(volatile uint32_t *)(WATCHDOG_BASE + (offset)))
#define WDOGLOAD    REG(0x000)
#define WDOGCONTROL REG(0x008)
#define WDOGINTCLR  REG(0x00c)
#define WDOGLOCK    REG(0xc00)
#define WDOG_UNLOCK 0x1acce551u
#define INTEN 1u
#define RESEN 2u

static int started;

static void unlock(void) { WDOGLOCK = WDOG_UNLOCK; }
static void lock(void) { WDOGLOCK = 0; }

void watchdog_start(uint32_t timeout_ms) {
    uint32_t ticks;
    if (timeout_ms == 0) timeout_ms = 1;
    /* 25 000 ticks/ms; cap so the 32-bit load cannot wrap. */
    if (timeout_ms > (0xffffffffu / (WATCHDOG_CLK_HZ / 1000u)))
        timeout_ms = 0xffffffffu / (WATCHDOG_CLK_HZ / 1000u);
    ticks = timeout_ms * (WATCHDOG_CLK_HZ / 1000u);
    unlock();
    WDOGCONTROL = 0;
    WDOGLOAD = ticks;
    WDOGINTCLR = 1;
    WDOGCONTROL = INTEN | RESEN;
    lock();
    started = 1;
}

void watchdog_kick(void) {
    if (!started) return;
    unlock();
    WDOGINTCLR = 1;
    lock();
}

void watchdog_stop(void) {
    unlock();
    WDOGCONTROL = 0;
    WDOGINTCLR = 1;
    lock();
    started = 0;
}

int watchdog_running(void) {
    return started;
}

#else
/* Host sanitizer builds have no CMSDK watchdog. */
void watchdog_start(uint32_t timeout_ms) { (void)timeout_ms; }
void watchdog_kick(void) {}
void watchdog_stop(void) {}
int watchdog_running(void) { return 0; }
#endif
