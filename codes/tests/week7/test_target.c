/* Unity running on Cortex-M7, using actual MMIO drivers and PRIMASK. */
#include "unity.h"
#include "irq.h"
#include "board.h"
#include "uart.h"
#include "watchdog.h"
#include "timebase.h"
#include "app_confirm.h"
#include "boot_config.h"
#include "uart_tx_test.h"
#include <string.h>
#define REG(a) (*(volatile uint32_t *)(a))
static flash_device dev;
static meta_flash_ops ops;
static int host(int op,const void *arg) {
    register int r0 __asm__("r0")=op;
    register const void *r1 __asm__("r1")=arg;
    __asm__ volatile("bkpt 0xab":"+r"(r0):"r"(r1):"memory");return r0;
}
void unity_putchar(int c) {char ch=(char)c;host(3,&ch);}
void exception_handler(uint32_t n) {(void)n;host(4,"TARGET EXCEPTION\n");for(;;){}}
void setUp(void) {watchdog_stop();__asm__ volatile("cpsie i" ::: "memory");}
void tearDown(void) {watchdog_stop();__asm__ volatile("cpsie i" ::: "memory");}
static uint32_t mask(void) {uint32_t n;__asm__ volatile("mrs %0,primask":"=r"(n));return n;}
static void test_irq_nested_unmasked(void) {
    uint32_t a=irq_save(),b=irq_save();TEST_ASSERT_EQUAL(0,a);TEST_ASSERT_EQUAL(1,b);
    irq_restore(b);TEST_ASSERT_EQUAL(1,mask());irq_restore(a);TEST_ASSERT_EQUAL(0,mask());
}
static void test_irq_nested_masked(void) {
    __asm__ volatile("cpsid i" ::: "memory");uint32_t a=irq_save(),b=irq_save();
    irq_restore(b);irq_restore(a);TEST_ASSERT_EQUAL(1,mask());
}
static void test_board_clock_and_vtor(void) {
    extern const uint32_t vectors[];board_init();TEST_ASSERT_EQUAL_UINT32(25000000,board_clock_hz());
    TEST_ASSERT_EQUAL_HEX32((uint32_t)vectors,REG(0xe000ed08u));
}
static void test_uart_init_divisor(void) {
    TEST_ASSERT_EQUAL(-1,uart_init(0,115200));TEST_ASSERT_EQUAL(-1,uart_init(25000000,0));
    TEST_ASSERT_EQUAL(0,uart_init(25000000,115200));TEST_ASSERT_EQUAL(217,REG(0x40004010u));
    uint8_t b;TEST_ASSERT_EQUAL(0,uart_getc(&b));TEST_ASSERT_EQUAL(-1,uart_getc(NULL));
}
static void test_uart_keeps_primask(void) {
    __asm__ volatile("cpsid i" ::: "memory");uint8_t b;
    TEST_ASSERT_EQUAL(0,uart_init(25000000,115200));TEST_ASSERT_EQUAL(1,mask());
    TEST_ASSERT_EQUAL(0,uart_getc(&b));TEST_ASSERT_EQUAL(1,mask());
}
static void test_uart_tx_timeout(void) {
    uart_init(25000000,115200);uart_tx_test_full=1;uart_tx_test_reads=0;uart_tx_test_data=0x55;
    TEST_ASSERT_EQUAL(-1,uart_write("A"));TEST_ASSERT_EQUAL_UINT32(1000000,uart_tx_test_reads);
    TEST_ASSERT_EQUAL_HEX32(0x55,uart_tx_test_data);uart_tx_test_full=0;
    TEST_ASSERT_EQUAL(0,uart_write("A"));TEST_ASSERT_EQUAL('A',uart_tx_test_data);
}
static void test_systick_clock_and_wrap(void) {
    timebase_init();TEST_ASSERT_EQUAL_UINT32(24999,REG(0xe000e014u));
    uint32_t start=timebase_ms();while((uint32_t)(timebase_ms()-start)<2) __asm__ volatile("nop");
    TEST_ASSERT_TRUE((uint32_t)(timebase_ms()-start)>=2);
    REG(0xe000e010u)=0;extern volatile uint32_t systick_ms;extern void SysTick_Handler(void);
    systick_ms=UINT32_MAX;SysTick_Handler();TEST_ASSERT_EQUAL(0,timebase_ms());
}
static void test_watchdog_registers(void) {
    watchdog_start(0);TEST_ASSERT_EQUAL_UINT32(25000,REG(0x40008000u));
    TEST_ASSERT_EQUAL(3,REG(0x40008008u));TEST_ASSERT_EQUAL(1,watchdog_running());
    watchdog_kick();watchdog_start(UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT32((UINT32_MAX/25000u)*25000u,REG(0x40008000u));
    watchdog_stop();TEST_ASSERT_EQUAL(0,REG(0x40008008u));TEST_ASSERT_EQUAL(0,watchdog_running());
}
static void fixture(cfg_record *c,meta_record *m) {
    flash_sim_bind(&dev,(volatile uint8_t *)FLASH_SIM_BASE,FLASH_SIZE);
    flash_erase(&dev,FLASH_PARAMS,0,0x4000);meta_flash_bind(&ops,&dev);
    TEST_ASSERT_EQUAL(META_OK,meta_init(&ops,m));TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,c));
}
static void test_app_confirm_launched(void) {
    cfg_record c;meta_record m;fixture(&c,&m);
    c.trial_pending=1;c.trial_slot=FLASH_APP;c.trial_stage=CFG_TRIAL_LAUNCHED;
    TEST_ASSERT_EQUAL(CFG_OK,cfg_commit(&ops,&c));watchdog_start(30000);
    TEST_ASSERT_EQUAL(0,week6_app_confirm());TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&c));
    TEST_ASSERT_EQUAL(0,c.trial_pending);TEST_ASSERT_EQUAL(FLASH_APP,c.last_confirmed_slot);
    TEST_ASSERT_EQUAL(0,watchdog_running());
}
static void test_old_app_cannot_confirm_inactive_trial(void) {
    cfg_record c;meta_record m;fixture(&c,&m);c.trial_pending=1;c.trial_slot=FLASH_BACKUP;
    c.trial_stage=CFG_TRIAL_ARMED;c.last_confirmed_slot=FLASH_APP;
    TEST_ASSERT_EQUAL(CFG_OK,cfg_commit(&ops,&c));TEST_ASSERT_EQUAL(0,week6_app_confirm());
    TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&c));TEST_ASSERT_EQUAL(1,c.trial_pending);
    TEST_ASSERT_EQUAL(CFG_TRIAL_ARMED,c.trial_stage);TEST_ASSERT_EQUAL(FLASH_APP,c.last_confirmed_slot);
}
static void test_app_cannot_confirm_unlaunched_trial(void) {
    cfg_record c;meta_record m;fixture(&c,&m);c.trial_pending=1;c.trial_slot=FLASH_APP;c.trial_stage=CFG_TRIAL_ARMED;
    TEST_ASSERT_EQUAL(CFG_OK,cfg_commit(&ops,&c));TEST_ASSERT_EQUAL(1,week6_app_confirm());
}
int main(void) {
    UNITY_BEGIN();RUN_TEST(test_irq_nested_unmasked);RUN_TEST(test_irq_nested_masked);
    RUN_TEST(test_board_clock_and_vtor);RUN_TEST(test_uart_init_divisor);RUN_TEST(test_uart_keeps_primask);
    RUN_TEST(test_uart_tx_timeout);RUN_TEST(test_systick_clock_and_wrap);RUN_TEST(test_watchdog_registers);
    RUN_TEST(test_app_confirm_launched);RUN_TEST(test_old_app_cannot_confirm_inactive_trial);
    RUN_TEST(test_app_cannot_confirm_unlaunched_trial);
    uint32_t args[2]={0x20026,(uint32_t)UNITY_END()};host(0x20,args);for(;;){}
}
