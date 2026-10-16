#include <stdint.h>
volatile uint32_t initialized_data = 0x12345678;
volatile uint32_t zero_data;
volatile uint32_t demo_result;
static int semihost(int operation, const void *argument) {
    register int r0 __asm__("r0") = operation;
    register const void *r1 __asm__("r1") = argument;
    __asm__ volatile ("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
int main(void) {
    const uint32_t cpuid = *(volatile uint32_t *)0xE000ED00;
    int ok = initialized_data == 0x12345678 && zero_data == 0
        && ((cpuid >> 4) & 0xfff) == 0xc27;
    demo_result = ok ? 0x600d : 0xdead;
    semihost(4, ok ? "PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99\n"
                   : "FAIL: Cortex-M7 or C runtime validation\n");
    uint32_t exit_block[2] = {0x20026, ok ? 0 : 1};
    semihost(0x20, exit_block);
    for (;;) { __asm__ volatile ("wfi"); }
}
