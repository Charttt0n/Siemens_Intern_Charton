#include "unity.h"
#define main legacy_main
#include "../test_ymodem.c"
#undef main
void setUp(void) {}
void tearDown(void) {}
static void unity_test_crc16_known_vectors(void) { TEST_ASSERT_EQUAL_INT(0, test_crc16_known_vectors()); }
static void unity_test_success_mixed_frames(void) { TEST_ASSERT_EQUAL_INT(0, test_success_mixed_frames()); }
static void unity_test_success_128_wrap(void) { TEST_ASSERT_EQUAL_INT(0, test_success_128_wrap()); }
static void unity_test_block0_validation(void) { TEST_ASSERT_EQUAL_INT(0, test_block0_validation()); }
static void unity_test_zero_length_file(void) { TEST_ASSERT_EQUAL_INT(0, test_zero_length_file()); }
static void unity_test_last_block_padding(void) { TEST_ASSERT_EQUAL_INT(0, test_last_block_padding()); }
static void unity_test_bad_crc_retransmit(void) { TEST_ASSERT_EQUAL_INT(0, test_bad_crc_retransmit()); }
static void unity_test_wrong_sequence_complement(void) { TEST_ASSERT_EQUAL_INT(0, test_wrong_sequence_complement()); }
static void unity_test_duplicates(void) { TEST_ASSERT_EQUAL_INT(0, test_duplicates()); }
static void unity_test_truncated_and_junk(void) { TEST_ASSERT_EQUAL_INT(0, test_truncated_and_junk()); }
static void unity_test_cancel(void) { TEST_ASSERT_EQUAL_INT(0, test_cancel()); }
static void unity_test_timeout_uses_clock(void) { TEST_ASSERT_EQUAL_INT(0, test_timeout_uses_clock()); }
static void unity_test_retry_limit_bad_frames(void) { TEST_ASSERT_EQUAL_INT(0, test_retry_limit_bad_frames()); }
static void unity_test_final_handshake_timeout(void) { TEST_ASSERT_EQUAL_INT(0, test_final_handshake_timeout()); }
static void unity_test_tx_failure(void) { TEST_ASSERT_EQUAL_INT(0, test_tx_failure()); }
static void unity_test_sink_failure(void) { TEST_ASSERT_EQUAL_INT(0, test_sink_failure()); }
static void unity_test_underflow_and_overflow(void) { TEST_ASSERT_EQUAL_INT(0, test_underflow_and_overflow()); }
static void unity_test_duplicate_terminator(void) { TEST_ASSERT_EQUAL_INT(0, test_duplicate_terminator()); }
static void unity_test_multi_file(void) { TEST_ASSERT_EQUAL_INT(0, test_multi_file()); }
static void unity_test_read_io_error(void) { TEST_ASSERT_EQUAL_INT(0, test_read_io_error()); }
static void unity_test_argument_checks(void) { TEST_ASSERT_EQUAL_INT(0, test_argument_checks()); }
static void unity_test_duplicate_bound(void) { TEST_ASSERT_EQUAL_INT(0, test_duplicate_bound()); }
static void unity_test_no_progress_time_budget(void) { TEST_ASSERT_EQUAL_INT(0, test_no_progress_time_budget()); }
static void unity_test_noise_bound(void) { TEST_ASSERT_EQUAL_INT(0, test_noise_bound()); }
static void unity_test_progress_resets_budget(void) { TEST_ASSERT_EQUAL_INT(0, test_progress_resets_budget()); }
static void unity_test_repeated_eot_bound(void) { TEST_ASSERT_EQUAL_INT(0, test_repeated_eot_bound()); }
static void unity_test_delayed_terminator(void) { TEST_ASSERT_EQUAL_INT(0, test_delayed_terminator()); }
static void unity_test_finish_committed_ack_failure(void) { TEST_ASSERT_EQUAL_INT(0, test_finish_committed_ack_failure()); }
static void unity_test_strict_io_contracts(void) { TEST_ASSERT_EQUAL_INT(0, test_strict_io_contracts()); }
static void unity_test_bad_frame_discard_io(void) { TEST_ASSERT_EQUAL_INT(0, test_bad_frame_discard_io()); }
static void unity_test_bad_frame_discard_crc_io(void) { TEST_ASSERT_EQUAL_INT(0, test_bad_frame_discard_crc_io()); }
static void unity_test_bad_frame_discard_budget(void) { TEST_ASSERT_EQUAL_INT(0, test_bad_frame_discard_budget()); }
int main(void) { UNITY_BEGIN();
RUN_TEST(unity_test_crc16_known_vectors);
RUN_TEST(unity_test_success_mixed_frames);
RUN_TEST(unity_test_success_128_wrap);
RUN_TEST(unity_test_block0_validation);
RUN_TEST(unity_test_zero_length_file);
RUN_TEST(unity_test_last_block_padding);
RUN_TEST(unity_test_bad_crc_retransmit);
RUN_TEST(unity_test_wrong_sequence_complement);
RUN_TEST(unity_test_duplicates);
RUN_TEST(unity_test_truncated_and_junk);
RUN_TEST(unity_test_cancel);
RUN_TEST(unity_test_timeout_uses_clock);
RUN_TEST(unity_test_retry_limit_bad_frames);
RUN_TEST(unity_test_final_handshake_timeout);
RUN_TEST(unity_test_tx_failure);
RUN_TEST(unity_test_sink_failure);
RUN_TEST(unity_test_underflow_and_overflow);
RUN_TEST(unity_test_duplicate_terminator);
RUN_TEST(unity_test_multi_file);
RUN_TEST(unity_test_read_io_error);
RUN_TEST(unity_test_argument_checks);
RUN_TEST(unity_test_duplicate_bound);
RUN_TEST(unity_test_no_progress_time_budget);
RUN_TEST(unity_test_noise_bound);
RUN_TEST(unity_test_progress_resets_budget);
RUN_TEST(unity_test_repeated_eot_bound);
RUN_TEST(unity_test_delayed_terminator);
RUN_TEST(unity_test_finish_committed_ack_failure);
RUN_TEST(unity_test_strict_io_contracts);
RUN_TEST(unity_test_bad_frame_discard_io);
RUN_TEST(unity_test_bad_frame_discard_crc_io);
RUN_TEST(unity_test_bad_frame_discard_budget);
return UNITY_END(); }
