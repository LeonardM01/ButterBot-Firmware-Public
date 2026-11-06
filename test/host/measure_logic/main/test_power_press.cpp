#include <unity.h>
#include "PowerPress.h"

static void test_a_tap_up_to_half_a_second_is_short(){
	TEST_ASSERT_EQUAL(PowerPress::Short, classifyPowerPress(0));
	TEST_ASSERT_EQUAL(PowerPress::Short, classifyPowerPress(120));
	TEST_ASSERT_EQUAL(PowerPress::Short, classifyPowerPress(500));
}

static void test_a_hold_of_one_to_two_and_a_half_seconds_is_medium(){
	TEST_ASSERT_EQUAL(PowerPress::Medium, classifyPowerPress(1000));
	TEST_ASSERT_EQUAL(PowerPress::Medium, classifyPowerPress(1700));
	TEST_ASSERT_EQUAL(PowerPress::Medium, classifyPowerPress(2500));
}

static void test_holds_between_the_windows_are_ignored(){
	// 0.5-1 s keeps a sloppy tap from starting a debug turn.
	TEST_ASSERT_EQUAL(PowerPress::Ignored, classifyPowerPress(501));
	TEST_ASSERT_EQUAL(PowerPress::Ignored, classifyPowerPress(999));
	// 2.5-3 s is a margin before the 3 s shutdown hold.
	TEST_ASSERT_EQUAL(PowerPress::Ignored, classifyPowerPress(2501));
	TEST_ASSERT_EQUAL(PowerPress::Ignored, classifyPowerPress(3000));
	TEST_ASSERT_EQUAL(PowerPress::Ignored, classifyPowerPress(10000));
}

void runPowerPressTests(){
	RUN_TEST(test_a_tap_up_to_half_a_second_is_short);
	RUN_TEST(test_a_hold_of_one_to_two_and_a_half_seconds_is_medium);
	RUN_TEST(test_holds_between_the_windows_are_ignored);
}
