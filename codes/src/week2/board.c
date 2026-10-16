#include "board.h"
extern const uint32_t vectors[];

void board_init(void) {
    /* QEMU AN500 supplies a fixed clock. No MCU-specific PLL exists here.
       VTOR is explicit so later application relocation has a clear owner. */
    *(volatile uint32_t *)0xE000ED08u = (uint32_t)vectors;
    __asm__ volatile ("dsb\nisb" ::: "memory");
}

uint32_t board_clock_hz(void) {
    return 25000000u;
}
