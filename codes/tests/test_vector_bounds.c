#include "image.h"
#include "crc32.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint8_t base[FLASH_SIZE], memory[FLASH_SIZE];
static unsigned total, failed;
static uint32_t get(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void test(const char *label,unsigned index,uint32_t value,int expect,int length) {
 memcpy(memory,base,FLASH_SIZE);uint8_t *h=memory+0x10000,*p=h+32;
 uint32_t size=get(h+12);
 if(length) {size=value;put(h+12,size);} else put(p+4*index,value);
 put(h+24,crc32(p,size));put(h+28,crc32(h,28));
 flash_device dev={0};image_info info={0};
 if(flash_sim_bind(&dev,memory,FLASH_SIZE)) {++failed;return;}
 int actual=image_validate(&dev,&info);++total;failed+=(actual!=expect);
 printf("VEC-%03u %s index=%u value=0x%08x expected=%d actual=%d %s\n",total,label,index,value,expect,actual,actual==expect?"PASS":"FAIL");
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 3;
 size_t n=fread(base,1,sizeof(base),f);int extra=fgetc(f);fclose(f);if(n!=sizeof(base)||extra!=EOF)return 4;
 uint32_t size=get(base+0x1000c),end=0x10000+size;
 const uint32_t msps[]={0,0x2000fff8,0x20010000,0x20010008,0x201ffff8,0x20200000,0x20200008,0xfffffff8};
 for(unsigned i=0;i<sizeof(msps)/sizeof(msps[0]);i++)test("MSP-bound",0,msps[i],(msps[i]>0x20010000&&msps[i]<=0x20200000)?0:7,0);
 for(unsigned r=1;r<8;r++)test("MSP-align",0,0x20010008+r,7,0);
 for(unsigned i=1;i<48;i++) {
  if((i>=7&&i<=10)||i==13){test("reserved-zero",i,0,0,0);test("reserved-nonzero",i,0x100c1,7,0);continue;}
  test("entry-lower",i,0x100c1,0,0);test("entry-below",i,0x100bf,7,0);
  test("entry-upper-inside",i,((end-1)&~1u)|1u,0,0);
  test("entry-at-end",i,end|1u,7,0);test("entry-above",i,(end+2)|1u,7,0);
  test("entry-even",i,0x100c0,7,0);test("entry-zero",i,0,7,0);
 }
 test("length-191",0,191,4,1);test("length-192-no-code",0,192,7,1);
 /* 193 bytes permits an address at the last byte; parser checks start address,
    not full Thumb instruction length. This acceptance is not executed. */
 printf("VECTOR_MATRIX total=%u failed=%u\n",total,failed);return failed?1:0;
}
