#include "unity.h"
#define main legacy_main
#include "../test_boot_meta.c"
#undef main
void setUp(void) {}
void tearDown(void) {}
static void unity_test_blank_then_init(void) { TEST_ASSERT_EQUAL_INT(0, test_blank_then_init()); }
static void unity_test_commit_alternates_pages(void) { TEST_ASSERT_EQUAL_INT(0, test_commit_alternates_pages()); }
static void unity_test_commit_invalid_fields_rejected(void) { TEST_ASSERT_EQUAL_INT(0, test_commit_invalid_fields_rejected()); }
static void unity_test_decode_rejects_fields_and_corruption(void) { TEST_ASSERT_EQUAL_INT(0, test_decode_rejects_fields_and_corruption()); }
static void unity_test_damaged_pages_require_recovery(void) { TEST_ASSERT_EQUAL_INT(0, test_damaged_pages_require_recovery()); }
static void unity_test_io_errors_are_distinct(void) { TEST_ASSERT_EQUAL_INT(0, test_io_errors_are_distinct()); }
static void unity_test_torn_erase_keeps_old_record(void) { TEST_ASSERT_EQUAL_INT(0, test_torn_erase_keeps_old_record()); }
static void unity_test_first_init_interrupted(void) { TEST_ASSERT_EQUAL_INT(0, test_first_init_interrupted()); }
static void unity_test_generation_wrap(void) { TEST_ASSERT_EQUAL_INT(0, test_generation_wrap()); }
static void unity_test_generation_ambiguity(void) { TEST_ASSERT_EQUAL_INT(0, test_generation_ambiguity()); }
static void unity_test_firmware_regions_untouched(void) { TEST_ASSERT_EQUAL_INT(0, test_firmware_regions_untouched()); }
static void unity_test_byte_tears(void) { TEST_ASSERT_EQUAL_INT(0, test_byte_tears()); }
static void unity_test_marker_tears(void) { TEST_ASSERT_EQUAL_INT(0, test_marker_tears()); }
static void unity_test_commit_readback_io(void) { TEST_ASSERT_EQUAL_INT(0, test_commit_readback_io()); }
static void unity_test_record_layout(void) { TEST_ASSERT_EQUAL_INT(0, test_record_layout()); }
static void unity_test_production_adapter(void) { TEST_ASSERT_EQUAL_INT(0, test_production_adapter()); }
int main(void) { UNITY_BEGIN();
RUN_TEST(unity_test_blank_then_init);
RUN_TEST(unity_test_commit_alternates_pages);
RUN_TEST(unity_test_commit_invalid_fields_rejected);
RUN_TEST(unity_test_decode_rejects_fields_and_corruption);
RUN_TEST(unity_test_damaged_pages_require_recovery);
RUN_TEST(unity_test_io_errors_are_distinct);
RUN_TEST(unity_test_torn_erase_keeps_old_record);
RUN_TEST(unity_test_first_init_interrupted);
RUN_TEST(unity_test_generation_wrap);
RUN_TEST(unity_test_generation_ambiguity);
RUN_TEST(unity_test_firmware_regions_untouched);
RUN_TEST(unity_test_byte_tears);
RUN_TEST(unity_test_marker_tears);
RUN_TEST(unity_test_commit_readback_io);
RUN_TEST(unity_test_record_layout);
RUN_TEST(unity_test_production_adapter);
return UNITY_END(); }
