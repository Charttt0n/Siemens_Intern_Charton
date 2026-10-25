#include "uart.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
volatile uint32_t uart_test_regs[5];
int main(void) {
    uart_test_regs[2]=1; uart_test_regs[1]=1; uart_test_regs[0]=0x55;
    assert(uart_flush()==-1); assert(uart_putc('A')==-1);
    assert(uart_write("ABC")==-1); assert(uart_test_regs[0]==0x55);
    puts("TX_FULL=1: flush/putc/write=-1; DATA=0x55 unchanged; bounded return PASS");
    uart_test_regs[1]=0;
    assert(uart_write("A")==0 && uart_test_regs[0]=='A');
    puts("TX_FULL=0 after timeout: write=0 DATA=0x41 PASS");
    uart_test_regs[2]=0; assert(uart_putc('B')==-1);
    assert(uart_write(0)==-1);
    puts("disabled TX and NULL: -1 PASS");
    return 0;
}
