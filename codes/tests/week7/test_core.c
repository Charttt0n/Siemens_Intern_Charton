#include "unity.h"
#include "control.h"
#include "secure_image.h"
#include "crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t memory[FLASH_SIZE], package[APP_EXEC_SIZE];
static uint32_t package_size;
static flash_device dev;
static meta_flash_ops real, ops;
static int fail_program, calls, tear, fail_erase, fail_read;
static int read_op(void *c,uint32_t o,void *p,uint32_t n) {
    if(fail_read) return -1;
    return real.read(c,o,p,n);
}
static int program_op(void *c,uint32_t o,const void *p,uint32_t n) {
    if(++calls==fail_program) {
        unsigned count=tear<0?0:(unsigned)tear; if(count>n) count=n;
        const uint8_t *b=p;
        for(unsigned i=0;i<count;i++) memory[0xf0000+o+i]&=b[i];
        return -1;
    }
    return real.program(c,o,p,n);
}
static int erase_op(void *c,uint32_t o,uint32_t n) {
    return fail_erase?-1:real.erase(c,o,n);
}
void setUp(void) {
    memset(memory,255,sizeof(memory));
    TEST_ASSERT_EQUAL(FLASH_OK,flash_sim_bind(&dev,memory,sizeof(memory)));
    meta_flash_bind(&real,&dev);ops=real;
    ops.read=read_op;ops.program=program_op;ops.erase=erase_op;
    fail_program=calls=fail_erase=fail_read=0;tear=-1;
}
void tearDown(void) {}
static void seed(void) { memcpy(memory+0x10000,package,package_size); }
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));}
static uint32_t oracle(const uint8_t *p,size_t n) {
    uint32_t s=UINT32_MAX;
    while(n--) {s^=*p++;for(unsigned b=0;b<8;b++) s=(s>>1)^((0u-(s&1))&0xedb88320u);}
    return s^UINT32_MAX;
}
static void test_crc_known(void) {
    TEST_ASSERT_EQUAL_HEX32(0,crc32(NULL,0));
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926,crc32("123456789",9));
}
static void test_crc_differential(void) {
    uint8_t data[4097];uint32_t rng=0x12345678;
    for(unsigned i=0;i<sizeof(data);i++){rng=rng*1664525u+1013904223u;data[i]=(uint8_t)(rng>>24);}
    for(unsigned n=0;n<=sizeof(data);n+=17) TEST_ASSERT_EQUAL_HEX32(oracle(data,n),crc32(data,n));
    for(unsigned split=0;split<sizeof(data);split+=31) {
        uint32_t s=crc32_update(UINT32_MAX,data,split);
        TEST_ASSERT_EQUAL_HEX32(oracle(data,sizeof(data)),crc32_update(s,data+split,sizeof(data)-split)^UINT32_MAX);
    }
}
static void test_decimal_bounds(void) {
    uint32_t n=99;
    TEST_ASSERT_TRUE(config_parse_u32(" 4294967295 ",&n));TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,n);
    TEST_ASSERT_TRUE(config_parse_u32("0",&n));TEST_ASSERT_EQUAL(0,n);
    const char *bad[]={"", " ","-1","+1","12x","1 2","4294967296","5000000000","18446744073709551616"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) TEST_ASSERT_FALSE(config_parse_u32(bad[i],&n));
    TEST_ASSERT_FALSE(config_parse_u32(NULL,&n));TEST_ASSERT_FALSE(config_parse_u32("1",NULL));
}
static void test_cfg_default_roundtrip(void) {
    cfg_record c,r;TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,&c));
    TEST_ASSERT_EQUAL(2500,c.window_ms);TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&r));
    TEST_ASSERT_EQUAL_MEMORY(&c,&r,sizeof(c));
    TEST_ASSERT_EQUAL(CFG_OK,config_set(&ops,&c,1,4000));TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&r));
    TEST_ASSERT_EQUAL(4000,r.window_ms);TEST_ASSERT_EQUAL(2,r.generation);
}
static void test_cfg_invalid_fields(void) {
    TEST_ASSERT_EQUAL(CFG_ARGUMENT,config_set(NULL,NULL,1,4000));
    cfg_record c;cfg_defaults(&c);c.window_ms=199;TEST_ASSERT_EQUAL(CFG_ARGUMENT,cfg_commit(&ops,&c));
    c.window_ms=60001;TEST_ASSERT_EQUAL(CFG_ARGUMENT,cfg_commit(&ops,&c));
    c.window_ms=2500;c.auto_boot=2;TEST_ASSERT_EQUAL(CFG_ARGUMENT,cfg_commit(&ops,&c));
    c.auto_boot=1;c.trial_slot=0;TEST_ASSERT_EQUAL(CFG_ARGUMENT,cfg_commit(&ops,&c));
}
static void test_cfg_corrupt_fail_closed(void) {
    cfg_record c;memory[0xf2000]=0;TEST_ASSERT_EQUAL(CFG_VERIFY,cfg_init(&ops,&c));
    TEST_ASSERT_EQUAL(0,calls);TEST_ASSERT_EQUAL_HEX8(0,memory[0xf2000]);
}
static void test_cfg_io_failure(void) {
    cfg_record c;cfg_defaults(&c);fail_read=1;TEST_ASSERT_EQUAL(CFG_IO,cfg_commit(&ops,&c));
    fail_read=0;fail_erase=1;TEST_ASSERT_EQUAL(CFG_IO,cfg_commit(&ops,&c));
}
static void test_cfg_torn_body(void) {
    for(int n=0;n<52;n++) {
        setUp();cfg_record c,r;TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,&c));
        c.window_ms=4000;fail_program=calls+1;tear=n;
        TEST_ASSERT_EQUAL(CFG_IO,cfg_commit(&ops,&c));TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&r));
        TEST_ASSERT_EQUAL(2500,r.window_ms);
    }
}
static void test_cfg_torn_marker(void) {
    for(int n=0;n<4;n++) {
        setUp();cfg_record c,r;TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,&c));
        c.window_ms=4000;fail_program=calls+2;tear=n;
        TEST_ASSERT_EQUAL(CFG_IO,cfg_commit(&ops,&c));TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&r));
        TEST_ASSERT_EQUAL(2500,r.window_ms);
    }
}
static void test_cfg_failed_save_keeps_runtime(void) {
    cfg_record c,old;TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,&c));old=c;fail_program=calls+1;
    TEST_ASSERT_EQUAL(CFG_IO,config_set(&ops,&c,1,4000));TEST_ASSERT_EQUAL_MEMORY(&old,&c,sizeof(c));
}
static void test_cfg_generation_wrap(void) {
    cfg_record c;TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,&c));
    put32(memory+0xf2008,UINT32_MAX);put32(memory+0xf2030,crc32(memory+0xf2000,48));
    TEST_ASSERT_EQUAL(CFG_OK,cfg_commit(&ops,&c));TEST_ASSERT_EQUAL(1,c.generation);
    TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&ops,&c));TEST_ASSERT_EQUAL(1,c.generation);
}
static void test_cfg_generation_ambiguous(void) {
    cfg_record c;TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,&c));
    memcpy(memory+0xf3000,memory+0xf2000,56);
    TEST_ASSERT_EQUAL(CFG_AMBIGUOUS,cfg_read(&ops,&c));
    TEST_ASSERT_EQUAL(CFG_AMBIGUOUS,cfg_commit(&ops,&c));
}
static void test_signed_valid(void) {seed();image_info i;TEST_ASSERT_EQUAL(SECURE_OK,secure_validate_slot(&dev,FLASH_APP,&i));TEST_ASSERT_EQUAL(1,i.version);}
static void test_signature_rejected(void) {seed();image_info i;memory[0x10000+package_size-1]^=1;TEST_ASSERT_EQUAL(SECURE_SIGNATURE,secure_validate_slot(&dev,FLASH_APP,&i));}
static void test_payload_rejected(void) {seed();image_info i;memory[0x10000+32+200]^=1;TEST_ASSERT_EQUAL(SECURE_IMAGE,secure_validate_slot(&dev,FLASH_APP,&i));}
static void test_header_signature_bound(void) {
    seed();image_info i;put32(memory+0x10000+20,99);put32(memory+0x10000+28,crc32(memory+0x10000,28));
    TEST_ASSERT_EQUAL(SECURE_SIGNATURE,secure_validate_slot(&dev,FLASH_APP,&i));
}
static void test_unsigned_rejected(void) {seed();image_info i;memset(memory+0x10000+package_size-272,255,272);TEST_ASSERT_EQUAL(SECURE_UNSIGNED,secure_validate_slot(&dev,FLASH_APP,&i));}
static void test_trailer_fields(void) {
    image_info i;
    for(unsigned off=4;off<=12;off+=4){seed();memory[0x10000+package_size-272+off]^=1;TEST_ASSERT_EQUAL(SECURE_TYPE,secure_validate_slot(&dev,FLASH_APP,&i));}
}
static void test_secure_invalid_arguments(void) {
    image_info i;TEST_ASSERT_EQUAL(SECURE_IO,secure_validate_slot(NULL,FLASH_APP,&i));
    TEST_ASSERT_EQUAL(SECURE_IO,secure_validate_slot(&dev,FLASH_APP,NULL));
    TEST_ASSERT_EQUAL(SECURE_IMAGE,secure_validate_slot(&dev,FLASH_PARAMS,&i));
}
static void ready_update(update_context *u,cfg_record *c) {
    ymodem_sink sink;seed();TEST_ASSERT_EQUAL(CFG_OK,cfg_init(&ops,c));
    TEST_ASSERT_EQUAL(UPDATE_OK,update_prepare(u,&dev,NULL,&real));update_sink_init(u,&sink);
    TEST_ASSERT_EQUAL(0,sink.begin(sink.context,"signed.pkg",package_size));
    TEST_ASSERT_EQUAL(0,sink.write(sink.context,package,package_size));
    TEST_ASSERT_EQUAL(0,sink.finish(sink.context,package_size));
}
static void test_signed_update_and_trial_order(void) {
    update_context u;cfg_record c,r;meta_record m;ready_update(&u,&c);
    TEST_ASSERT_EQUAL(UPDATE_OK,trial_activate(&u,&ops,&c));
    TEST_ASSERT_EQUAL(CFG_OK,cfg_read(&real,&r));TEST_ASSERT_EQUAL(CFG_TRIAL_ARMED,r.trial_stage);
    TEST_ASSERT_EQUAL(FLASH_BACKUP,r.trial_slot);TEST_ASSERT_EQUAL(FLASH_APP,r.last_confirmed_slot);
    TEST_ASSERT_EQUAL(META_OK,meta_read(&real,&m));TEST_ASSERT_EQUAL(FLASH_BACKUP,m.active_slot);
}
static void test_trial_config_failure_blocks_activation(void) {
    update_context u;cfg_record c;meta_record m;ready_update(&u,&c);fail_program=calls+1;
    TEST_ASSERT_EQUAL(UPDATE_META,trial_activate(&u,&ops,&c));
    TEST_ASSERT_EQUAL(META_OK,meta_read(&real,&m));TEST_ASSERT_EQUAL(FLASH_APP,m.active_slot);
}
static void test_update_invalid_signature_not_ready(void) {
    update_context u;ymodem_sink sink;meta_record m;seed();
    TEST_ASSERT_EQUAL(UPDATE_OK,update_prepare(&u,&dev,NULL,NULL));update_sink_init(&u,&sink);
    TEST_ASSERT_EQUAL(0,sink.begin(sink.context,"bad.pkg",package_size));
    uint8_t bad[APP_EXEC_SIZE];memcpy(bad,package,package_size);bad[package_size-1]^=1;
    TEST_ASSERT_EQUAL(0,sink.write(sink.context,bad,package_size));TEST_ASSERT_NOT_EQUAL(0,sink.finish(sink.context,package_size));
    TEST_ASSERT_EQUAL(UPDATE_STATE,trial_activate(&u,&ops,&(cfg_record){0}));
    TEST_ASSERT_EQUAL(META_OK,meta_read(&real,&m));TEST_ASSERT_EQUAL(FLASH_APP,m.active_slot);
}
static void test_signed_update_length_bounds(void) {
    update_context u;ymodem_sink sink;
    TEST_ASSERT_EQUAL(UPDATE_OK,update_prepare(&u,&dev,NULL,NULL));update_sink_init(&u,&sink);
    TEST_ASSERT_NOT_EQUAL(0,sink.begin(sink.context,"small",IMAGE_HEADER_SIZE+APP_VECTOR_SIZE+SIG_TRAILER_SIZE-1));
    TEST_ASSERT_NOT_EQUAL(0,sink.begin(sink.context,"large",APP_EXEC_SIZE+1));
    TEST_ASSERT_EQUAL(0,sink.begin(sink.context,"max",APP_EXEC_SIZE));
    TEST_ASSERT_EQUAL_UINT32(SECURE_MAX_PAYLOAD,u.payload_expected);
    sink.abort(sink.context);TEST_ASSERT_EQUAL(UPDATE_STATE,update_activate(&u));
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;FILE *f=fopen(argv[1],"rb");if(!f) return 2;
    package_size=(uint32_t)fread(package,1,sizeof(package),f);fclose(f);
    if(package_size<500 || package_size==sizeof(package)) return 2;
    UNITY_BEGIN();
    RUN_TEST(test_crc_known);RUN_TEST(test_crc_differential);RUN_TEST(test_decimal_bounds);
    RUN_TEST(test_cfg_default_roundtrip);RUN_TEST(test_cfg_invalid_fields);RUN_TEST(test_cfg_corrupt_fail_closed);
    RUN_TEST(test_cfg_io_failure);RUN_TEST(test_cfg_torn_body);RUN_TEST(test_cfg_torn_marker);
    RUN_TEST(test_cfg_failed_save_keeps_runtime);RUN_TEST(test_cfg_generation_wrap);RUN_TEST(test_cfg_generation_ambiguous);
    RUN_TEST(test_signed_valid);RUN_TEST(test_signature_rejected);RUN_TEST(test_payload_rejected);
    RUN_TEST(test_header_signature_bound);RUN_TEST(test_unsigned_rejected);RUN_TEST(test_trailer_fields);RUN_TEST(test_secure_invalid_arguments);
    RUN_TEST(test_signed_update_and_trial_order);RUN_TEST(test_trial_config_failure_blocks_activation);
    RUN_TEST(test_update_invalid_signature_not_ready);
    RUN_TEST(test_signed_update_length_bounds);
    return UNITY_END();
}
