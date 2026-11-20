/* Test ELF only. No wrapper symbol is linked into production. Fault selector
 * is the final word of the simulated NOR; production never consults it. */
#include "flash.h"
#include "boot_meta.h"
#include "boot_config.h"
volatile uint32_t fault_latched;
static int used, activating, arming;
extern int __real_flash_read(const flash_device *, flash_partition_id, uint32_t, void *, uint32_t);
extern int __real_flash_program(flash_device *, flash_partition_id, uint32_t, const void *, uint32_t);
extern int __real_flash_erase(flash_device *, flash_partition_id, uint32_t, uint32_t);
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t id(const flash_device *d) { return le32((const uint8_t *)(d->memory + FLASH_SIZE - 4)); }
__attribute__((noinline,noreturn)) static void latch(uint32_t n) {
    __asm__ volatile("cpsid i" ::: "memory"); fault_latched=n;
    __asm__ volatile("dsb\nisb" ::: "memory"); for (;;) __asm__ volatile("wfi");
}
static void prefix(flash_device *d, flash_partition_id s,uint32_t off,const uint8_t *p,unsigned n) {
    for(unsigned i=0;i<n;i++) d->memory[flash_partition_get(s)->offset+off+i] &= p[i];
}
int __wrap_flash_read(const flash_device *d,flash_partition_id s,uint32_t off,void *out,uint32_t n) {
    if(!used && id(d)==12 && s==FLASH_BACKUP && off==64) { used=1;return FLASH_ARGUMENT; }
    if(id(d)==13 && s==FLASH_APP) return FLASH_ARGUMENT;
    return __real_flash_read(d,s,off,out,n);
}
int __wrap_flash_erase(flash_device *d,flash_partition_id s,uint32_t off,uint32_t n) {
    uint32_t f=id(d);
    if(s==FLASH_BACKUP && f==1) {
        for(unsigned i=0;i<73;i++) d->memory[flash_partition_get(s)->offset+off+i]=255;
        latch(f);
    }
    if(!used && s==FLASH_BACKUP && f==10) {used=1;return FLASH_ARGUMENT;}
    return __real_flash_erase(d,s,off,n);
}
int __wrap_flash_program(flash_device *d,flash_partition_id s,uint32_t off,const void *in,uint32_t n) {
    uint32_t f=id(d); const uint8_t *p=in;
    if(s==FLASH_BACKUP && off==64 && f==2) {prefix(d,s,off,p,2);latch(f);}
    if(!used && s==FLASH_BACKUP && off==64 && f==11) {used=1;return FLASH_ARGUMENT;}
    if(s==FLASH_PARAMS && n==52 && off>=CFG_PAGE_BASE) {
        arming=le32(p+28)==1 && le32(p+32)==CFG_TRIAL_ARMED;
        if(arming && f==3) {prefix(d,s,off,p,30);latch(f);}
        if(arming && f==14) {used=1;return FLASH_ARGUMENT;}
        if(f==15 && le32(p+12)==4000) {used=1;return FLASH_ARGUMENT;}
    }
    if(s==FLASH_PARAMS && n==44 && off<CFG_PAGE_BASE) {
        activating=le32(p+12)==FLASH_BACKUP && le32(p+20)==META_STATE_IDLE;
        if(activating && f==5) {prefix(d,s,off,p,42);latch(f);}
    }
    if(s==FLASH_PARAMS && n==4 && off%META_PAGE_SIZE==44 && activating && f==6) {
        prefix(d,s,off,p,2);latch(f);
    }
    int result=__real_flash_program(d,s,off,in,n);
    if(s==FLASH_PARAMS && n==4 && off%META_PAGE_SIZE==52 && arming && f==4) latch(f);
    if(s==FLASH_PARAMS && n==4 && off%META_PAGE_SIZE==44 && activating && f==7) latch(f);
    return result;
}
