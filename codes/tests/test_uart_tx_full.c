#include <stdint.h>
#include "uart.h"
#include "uart_tx_test.h"
static int host(int op, const void *arg) {
    register int r0 __asm__("r0")=op;
    register const void *r1 __asm__("r1")=arg;
    __asm__ volatile("bkpt 0xab":"+r"(r0):"r"(r1):"memory");return r0;
}
static int check(int ok,const char *label) { host(4,label);host(4,ok?" OK\n":" FAIL\n");return ok; }
void exception_handler(uint32_t n) {(void)n;for(;;){}}
int main(void) {
    int ok=1;
    ok &= check(uart_init(25000000,115200)==0,"INIT");
    uart_tx_test_full=1;uart_tx_test_reads=0;uart_tx_test_data=0x55;
    ok &= check(uart_flush()==-1,"FULL_FLUSH_RETURN=-1");
    ok &= check(uart_tx_test_reads==1000000,"FULL_FLUSH_READS=1000000");
    ok &= check(uart_putc('A')==-1,"FULL_PUTC_RETURN=-1");
    ok &= check(uart_tx_test_reads==2000000,"FULL_PUTC_TOTAL_READS=2000000");
    ok &= check(uart_write("ABC")==-1,"FULL_WRITE_RETURN=-1");
    ok &= check(uart_tx_test_reads==3000000,"FULL_WRITE_TOTAL_READS=3000000");
    ok &= check(uart_tx_test_data==0x55,"FULL_DATA_UNCHANGED=0x55");
    uart_tx_test_full=0;uart_tx_test_reads=0;
    ok &= check(uart_write("A")==0,"RECOVER_WRITE_RETURN=0");
    ok &= check(uart_tx_test_data==0x41 && uart_tx_test_reads==1,"RECOVER_DATA=0x41_READS=1");
    host(4,ok?"TX_FULL_CONTRACT_PASS\n":"TX_FULL_CONTRACT_FAIL\n");
    uint32_t args[2]={0x20026,ok?0:1};host(0x20,args);for(;;){}
}
