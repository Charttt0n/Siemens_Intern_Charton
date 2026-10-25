#ifndef BOOT_IRQ_H
#define BOOT_IRQ_H
#include <stdint.h>
/* Single-core privileged Cortex-M critical-section contract:
 * irq_save() returns the entry PRIMASK (0 or 1), then sets PRIMASK=1.
 * Pair each call with irq_restore(its_saved_value) on every exit path.
 * Nested calls must restore in LIFO order; never substitute a constant zero.
 * PRIMASK=1 masks configurable-priority exceptions, not NMI or HardFault.
 * Restoring zero only removes the PRIMASK mask; BASEPRI, FAULTMASK and NVIC
 * state remain unchanged. No pending interrupt is cleared by either function.
 * The memory clobber is a compiler barrier, not a DMB/DSB hardware barrier.
 * This does not provide multicore synchronization or a boot handoff.
 */
static inline uint32_t irq_save(void) {
    uint32_t saved;
    __asm__ volatile ("mrs %0, primask\ncpsid i" : "=r"(saved) :: "memory");
    return saved;
}
static inline void irq_restore(uint32_t saved) {
    __asm__ volatile ("msr primask, %0" :: "r"(saved) : "memory");
}
#endif
