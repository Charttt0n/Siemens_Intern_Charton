/* Test-only NOR fault model, linked only into bootloader-week5-fault.elf.
 * The last params page's first word selects one scenario (little endian).
 * Wrappers preserve normal range/alignment checks on normal operations; torn
 * operations directly change the specified prefix to model a physical tear.
 * fault_latched is observed through QMP, then the host pauses/snapshots NOR.
 */
#include "boot_meta.h"
#include "uart.h"
#include "timebase.h"
#define CONTROL_OFFSET 0xff000u
volatile uint32_t fault_latched;
static int activating;
static int fault_used;
int __real_flash_program(flash_device *, flash_partition_id, uint32_t, const void *, uint32_t);
int __real_flash_erase(flash_device *, flash_partition_id, uint32_t, uint32_t);
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t scenario(flash_device *dev) {
    uint32_t value = 0;
    for (unsigned i=0; i<4; ++i) value |= (uint32_t)dev->memory[CONTROL_OFFSET+i] << (i*8);
    return value;
}
__attribute__((noreturn,noinline)) static void latch(uint32_t id) {
    __asm__ volatile ("cpsid i" ::: "memory");
    fault_latched = id;
    __asm__ volatile ("dsb\nisb" ::: "memory");
    for (;;) __asm__ volatile ("wfi");
}
static void torn_program(flash_device *dev, flash_partition_id slot,
                         uint32_t offset, const uint8_t *data, uint32_t count) {
    uint32_t base = flash_partition_get(slot)->offset + offset;
    for (uint32_t i=0; i<count; ++i) dev->memory[base+i] &= data[i];
}
int __wrap_flash_erase(flash_device *dev, flash_partition_id slot,
                       uint32_t offset, uint32_t size) {
    uint32_t id = scenario(dev);
    if (!fault_used && slot == FLASH_BACKUP && id == 1u) {
        for (unsigned i=0; i<73; ++i) dev->memory[0x80000u+offset+i] = 0xffu;
        latch(id);
    }
    if (!fault_used && slot == FLASH_BACKUP && id == 10u) { fault_used=1; return FLASH_ARGUMENT; }
    if (slot == FLASH_PARAMS && id == 3u) {
        meta_flash_ops ops;
        meta_record current;
        meta_flash_bind(&ops, dev);
        if (meta_read(&ops, &current) == META_OK && current.state == META_STATE_READY) {
            for (unsigned i=0; i<24; ++i) dev->memory[0xf0000u+offset+i] = 0xffu;
            latch(id);
        }
    }
    int result = __real_flash_erase(dev, slot, offset, size);
    if (!fault_used && slot == FLASH_BACKUP && id == 13u) {
        fault_used=1; ++uart_rx_dropped; /* inject the driver's loss indication */
    }
    if (slot == FLASH_BACKUP && id == 14u) {
        /* A sender that ignores flow control can fill the real IRQ ring while
           the foreground is occupied with storage. Keep interrupts enabled. */
        uint32_t start = timebase_ms();
        while ((uint32_t)(timebase_ms()-start) < 300u) __asm__ volatile ("nop");
    }
    return result;
}
int __wrap_flash_program(flash_device *dev, flash_partition_id slot,
                         uint32_t offset, const void *input, uint32_t size) {
    const uint8_t *data = input;
    uint32_t id = scenario(dev);
    if (slot == FLASH_BACKUP && offset == 64u && id == 2u) {
        torn_program(dev, slot, offset, data, 2); latch(id);
    }
    if (!fault_used && slot == FLASH_BACKUP && offset == 64u && id == 11u) {
        fault_used=1; return FLASH_ARGUMENT;
    }
    if (slot == FLASH_PARAMS && size == 44u) {
        activating = get32(data+20) == META_STATE_IDLE && get32(data+12) == FLASH_BACKUP;
        if (activating && (id == 4u || id == 5u)) {
            torn_program(dev, slot, offset, data, id == 4u ? 21u : 42u); latch(id);
        }
        if (activating && id == 12u) { fault_used=1; return FLASH_ARGUMENT; }
        if (get32(data+20) == META_STATE_VERIFYING && id == 9u) {
            torn_program(dev, slot, offset, data, 21u); latch(id);
        }
    }
    if (slot == FLASH_PARAMS && size == 4u && offset % META_PAGE_SIZE == 44u) {
        if (activating && id == 6u) { torn_program(dev, slot, offset, data, 2u); latch(id); }
        int result = __real_flash_program(dev, slot, offset, input, size);
        if (activating && id == 7u) latch(id);
        if (id == 8u) {
            meta_flash_ops ops; meta_record record;
            meta_flash_bind(&ops, dev);
            if (meta_read(&ops, &record) == META_OK && record.state == META_STATE_READY) latch(id);
        }
        return result;
    }
    return __real_flash_program(dev, slot, offset, input, size);
}
