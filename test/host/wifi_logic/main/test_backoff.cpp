#include <unity.h>
#include "WiFiBackoff.h"

static void test_first_retry_waits_one_second(){
	TEST_ASSERT_EQUAL_UINT32(1000, WiFiBackoff::delayMs(0));
}

static void test_delay_doubles_on_each_failed_attempt(){
	TEST_ASSERT_EQUAL_UINT32(2000, WiFiBackoff::delayMs(1));
	TEST_ASSERT_EQUAL_UINT32(4000, WiFiBackoff::delayMs(2));
	TEST_ASSERT_EQUAL_UINT32(8000, WiFiBackoff::delayMs(3));
	TEST_ASSERT_EQUAL_UINT32(16000, WiFiBackoff::delayMs(4));
	TEST_ASSERT_EQUAL_UINT32(32000, WiFiBackoff::delayMs(5));
}

static void test_delay_is_capped_at_sixty_seconds(){
	TEST_ASSERT_EQUAL_UINT32(60000, WiFiBackoff::delayMs(6));
	TEST_ASSERT_EQUAL_UINT32(60000, WiFiBackoff::delayMs(7));
}

static void test_cap_holds_for_attempt_counts_beyond_the_shift_width(){
	// Reconnects run forever, so the attempt counter grows without bound.
	TEST_ASSERT_EQUAL_UINT32(60000, WiFiBackoff::delayMs(31));
	TEST_ASSERT_EQUAL_UINT32(60000, WiFiBackoff::delayMs(32));
	TEST_ASSERT_EQUAL_UINT32(60000, WiFiBackoff::delayMs(1000));
	TEST_ASSERT_EQUAL_UINT32(60000, WiFiBackoff::delayMs(UINT32_MAX));
}

void runBackoffTests(){
	RUN_TEST(test_first_retry_waits_one_second);
	RUN_TEST(test_delay_doubles_on_each_failed_attempt);
	RUN_TEST(test_delay_is_capped_at_sixty_seconds);
	RUN_TEST(test_cap_holds_for_attempt_counts_beyond_the_shift_width);
}
