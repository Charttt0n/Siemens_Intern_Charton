#include "unity.h"
#define main legacy_main
#include "../test_image_slot.c"
#undef main
void setUp(void) {}
void tearDown(void) {}
static void image_contract(void) {TEST_ASSERT_EQUAL_INT(0,legacy_main());}
int main(void) {UNITY_BEGIN();RUN_TEST(image_contract);return UNITY_END();}
