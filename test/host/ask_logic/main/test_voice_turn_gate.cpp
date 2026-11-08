#include <unity.h>
#include "VoiceTurnGate.h"

namespace {

using VoiceTurnGate::Verdict;

// The lowest idle values of four question turns on the robot (docs/voice-turn-memory.md, "Observed on the robot").
constexpr size_t IdleFree = 40735;
constexpr size_t IdleLargest = 25600;

void test_an_idle_robot_may_listen(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, IdleFree, IdleLargest));
}

// #22 review, finding 3: the ask worker still reads the capture buffer, so no new recording may start.
void test_a_running_ask_refuses_however_much_ram_is_free(){
	TEST_ASSERT_EQUAL(Verdict::AskStillRunning, VoiceTurnGate::check(true, IdleFree, IdleLargest));
	TEST_ASSERT_EQUAL(Verdict::AskStillRunning, VoiceTurnGate::check(true, 0, 0));
}

// A turn took up to 38,116 B (idle 41,135 -> low 3,019): starting below that could run out.
void test_less_free_than_a_turn_has_taken_refuses(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 38116, IdleLargest));
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 38911, IdleLargest));
}

void test_the_state_that_rebooted_in_20_is_refused(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 27907, 12288)); // turn5.log, turn 2
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, IdleFree, 12288));
}

void test_too_small_a_largest_block_refuses(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, IdleFree, 16383));
}

void test_exactly_the_thresholds_is_enough(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, 38912, 16384));
}

}

void runVoiceTurnGateTests(){
	RUN_TEST(test_an_idle_robot_may_listen);
	RUN_TEST(test_a_running_ask_refuses_however_much_ram_is_free);
	RUN_TEST(test_less_free_than_a_turn_has_taken_refuses);
	RUN_TEST(test_the_state_that_rebooted_in_20_is_refused);
	RUN_TEST(test_too_small_a_largest_block_refuses);
	RUN_TEST(test_exactly_the_thresholds_is_enough);
}
