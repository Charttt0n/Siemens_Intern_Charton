#include "unity.h"
#include "../test_flash.c"
static uint8_t nor[FLASH_SIZE];
void setUp(void) {}
void tearDown(void) {}
static void flash_contract(void) {TEST_ASSERT_EQUAL_INT(0,flash_selftest(nor));}
int main(void) {UNITY_BEGIN();RUN_TEST(flash_contract);return UNITY_END();}
