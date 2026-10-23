#include "image.h"
#include "uart.h"
#include "board.h"
#define MMIO(addr) (*(volatile uint32_t *)(addr))
volatile uint32_t boot_status, exception_number;
image_info boot_image;
__attribute__((noreturn)) void boot_idle(void) { for (;;) __asm__ volatile ("wfi"); }
__attribute__((noreturn)) void exception_handler(uint32_t number) {
    exception_number = number; boot_status = 0x100u; boot_idle();
}
static void print(const char *text) {
    if (uart_write(text)) { boot_status = 0x101u; boot_idle(); }
}
static void hex(uint32_t value) {
    char text[] = "0x00000000\r\n";
    const char digits[] = "0123456789abcdef";
    for (unsigned i=0; i<8; ++i) text[2+i]=digits[(value>>(28-4*i))&15];
    print(text);
}
__attribute__((noreturn)) static void reject(int result) {
    boot_status = (uint32_t)result;
    print("REJECT: "); hex(boot_status); print("> "); boot_idle();
}
extern void jump_to_app(uint32_t msp, uint32_t reset) __attribute__((noreturn));

__attribute__((noinline,noreturn)) void boot_handoff(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
    MMIO(0x40004008u) = 0; /* Quiesce UART before disabling its NVIC input. */
    MMIO(0x4000400cu) = 0xf;
    MMIO(0x40004004u) = 0xc;
    MMIO(0xe000e010u) = 0; /* SysTick CTRL */
    MMIO(0xe000e014u) = 0;
    MMIO(0xe000e018u) = 0;
    MMIO(0xe000e180u) = UINT32_MAX; /* AN500 has exactly 32 external IRQs. */
    MMIO(0xe000e280u) = UINT32_MAX;
    MMIO(0xe000ed04u) = (1u<<25) | (1u<<27); /* Clear SysTick/PendSV pending. */
    MMIO(0xe000ed08u) = APP_EXEC_BASE;
    __asm__ volatile ("dsb\nisb" ::: "memory");
    jump_to_app(boot_image.msp, boot_image.reset);
}

int main(void) {
    flash_device storage = {0};
    board_init();
    if (uart_init(board_clock_hz(), 115200u)) { boot_status = 0x101u; boot_idle(); }
    print("BOOT: stage4 image validation\r\n");
    if (flash_sim_bind(&storage, (volatile uint8_t *)FLASH_SIM_BASE, FLASH_SIZE)) reject(IMAGE_IO);
    int result = image_validate(&storage, &boot_image);
    if (result) reject(result);
    print("IMAGE: CRC32 PASS\r\n");
    result = image_load(&storage, &boot_image);
    if (!result) result = boot_verify_loaded(&boot_image);
    if (result) reject(result);
    print("LOAD: verified at 0x00010000\r\nJUMP: application\r\n");
    if (uart_flush()) { boot_status = 0x101u; boot_idle(); }
    boot_status = 0;
    boot_handoff();
}
