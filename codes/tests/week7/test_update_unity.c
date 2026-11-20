#include "unity.h"
#define main legacy_main
#include "../test_update.c"
#undef main
void setUp(void) {}
void tearDown(void) {}
static void unity_test_success_update(void) { TEST_ASSERT_EQUAL_INT(0, test_success_update()); }
static void unity_test_bad_image_failure(void) { TEST_ASSERT_EQUAL_INT(0, test_bad_image_failure()); }
static void unity_test_erase_failure_keeps_receiving_order(void) { TEST_ASSERT_EQUAL_INT(0, test_erase_failure_keeps_receiving_order()); }
static void unity_test_write_failure(void) { TEST_ASSERT_EQUAL_INT(0, test_write_failure()); }
static void unity_test_abort_and_failure_record(void) { TEST_ASSERT_EQUAL_INT(0, test_abort_and_failure_record()); }
static void unity_test_length_bounds(void) { TEST_ASSERT_EQUAL_INT(0, test_length_bounds()); }
static void unity_test_yodem_end_to_end(void) { TEST_ASSERT_EQUAL_INT(0, test_yodem_end_to_end()); }
static void unity_test_argument_and_state_guards(void) { TEST_ASSERT_EQUAL_INT(0, test_argument_and_state_guards()); }
static void unity_test_activation(void) { TEST_ASSERT_EQUAL_INT(0, test_activation()); }
int main(void) { UNITY_BEGIN();
RUN_TEST(unity_test_success_update);
RUN_TEST(unity_test_bad_image_failure);
RUN_TEST(unity_test_erase_failure_keeps_receiving_order);
RUN_TEST(unity_test_write_failure);
RUN_TEST(unity_test_abort_and_failure_record);
RUN_TEST(unity_test_length_bounds);
RUN_TEST(unity_test_yodem_end_to_end);
RUN_TEST(unity_test_argument_and_state_guards);
RUN_TEST(unity_test_activation);
return UNITY_END(); }
