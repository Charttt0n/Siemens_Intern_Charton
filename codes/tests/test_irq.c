#include <stdint.h>
#include "irq.h"
#include "uart.h"
static uint32_t mask(void) { uint32_t s; __asm__ volatile("mrs %0, primask":"=r"(s)); return s; }
static int host(int op, const void *arg) {
    register int r0 __asm__("r0")=op;
    register const void *r1 __asm__("r1")=arg;
    __asm__ volatile("bkpt 0xab":"+r"(r0):"r"(r1):"memory"); return r0;
}
static void hex(uint32_t value) {
    char text[11]="0x00000000";
    const char digits[]="0123456789abcdef";
    for (unsigned i=0;i<8;++i) text[9-i]=digits[(value>>(4*i))&15u];
    host(4,text);
}
static int check(const char *id, uint32_t value, uint32_t expected) {
    host(4,id); host(4," actual="); hex(value); host(4," expected="); hex(expected);
    host(4,value==expected ? " OK\n" : " FAIL\n"); return value==expected;
}
void exception_handler(uint32_t n) { (void)n; for(;;){} }
int main(void) {
    int ok=1; uint8_t byte;
    for (uint32_t initial=0; initial<2; ++initial) {
        int group=1;
        /* Independent setup: do not use the restore function under test. */
        if(initial) __asm__ volatile("cpsid i" ::: "memory");
        else __asm__ volatile("cpsie i" ::: "memory");
        host(4,initial ? "GROUP initial=1\n" : "GROUP initial=0\n");
        group &= check("ENTRY",mask(),initial);
        uint32_t outer=irq_save();
        group &= check("SAVE_RETURN",outer,initial);
        group &= check("SAVE_MASK",mask(),1);
        uint32_t inner=irq_save();
        group &= check("NEST_RETURN",inner,1);
        group &= check("NEST_MASK",mask(),1);
        irq_restore(inner); group &= check("INNER_RESTORE",mask(),1);
        irq_restore(outer); group &= check("OUTER_RESTORE",mask(),initial);
        group &= check("UART_INIT_RETURN",(uint32_t)uart_init(25000000,115200),0);
        group &= check("UART_INIT_MASK",mask(),initial);
        group &= check("UART_GETC_RETURN",(uint32_t)uart_getc(&byte),0);
        group &= check("UART_GETC_MASK",mask(),initial);
        ok &= group;
        host(4,group ? (initial ? "PRIMASK initial=1 nested/init/getc restored=1 PASS\n" : "PRIMASK initial=0 nested/init/getc restored=0 PASS\n") : "PRIMASK FAIL\n");
    }
    __asm__ volatile("cpsie i" ::: "memory");
    uint32_t args[2]={0x20026,ok?0:1}; host(0x20,args); for(;;){}
}
