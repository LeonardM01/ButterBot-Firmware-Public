#include <unity.h>
#include "VoiceTurnGate.h"

namespace {

using VoiceTurnGate::Verdict;

// Idle values the robot is expected to start a turn from (docs/voice-turn-memory.md): well over both thresholds.
constexpr size_t IdleFree = 35300;
constexpr size_t IdleLargest = 26600;

void test_an_idle_robot_may_listen(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, IdleFree, IdleLargest));
}

// #22 review, finding 3: the ask worker still reads the capture buffer, so no new recording may start.
void test_a_running_ask_refuses_however_much_ram_is_free(){
	TEST_ASSERT_EQUAL(Verdict::AskStillRunning, VoiceTurnGate::check(true, IdleFree, IdleLargest));
	TEST_ASSERT_EQUAL(Verdict::AskStillRunning, VoiceTurnGate::check(true, 0, 0));
}

void test_too_little_free_internal_ram_refuses(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 27907, IdleLargest)); // turn5.log's crash
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 30719, IdleLargest));
}

void test_too_small_a_largest_block_refuses(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, IdleFree, 7679));
}

void test_exactly_the_thresholds_is_enough(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, 30720, 7680));
}

}

void runVoiceTurnGateTests(){
	RUN_TEST(test_an_idle_robot_may_listen);
	RUN_TEST(test_a_running_ask_refuses_however_much_ram_is_free);
	RUN_TEST(test_too_little_free_internal_ram_refuses);
	RUN_TEST(test_too_small_a_largest_block_refuses);
	RUN_TEST(test_exactly_the_thresholds_is_enough);
}
