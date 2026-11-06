#include "timebase.h"

#define SYST_CSR (*(volatile uint32_t *)0xe000e010u)
#define SYST_RVR (*(volatile uint32_t *)0xe000e014u)
#define SYST_CVR (*(volatile uint32_t *)0xe000e018u)

volatile uint32_t systick_ms;

/* Strong override of the weak vector entry in src/week3/startup.S. */
void SysTick_Handler(void) {
    ++systick_ms;
}

void timebase_init(void) {
    SYST_CSR = 0;
    SYST_RVR = 24999u; /* 25 MHz / 1000 - 1 */
    SYST_CVR = 0;
    SYST_CSR = 7u; /* core clock, interrupt, enable */
    __asm__ volatile ("dsb\nisb" ::: "memory");
}

uint32_t timebase_ms(void) {
    return systick_ms;
}
