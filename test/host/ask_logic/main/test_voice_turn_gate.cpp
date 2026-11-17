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

// 17 Nov bench log: with the controller connected over BLE the robot idled at 37,939 / 24,576, and after an RC
// session at 38,463 / 24,576. The old 38,912 guard refused every turn in both states ("Oops, try again").
void test_a_connected_controller_or_a_past_rc_session_may_listen(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, 37939, 24576));
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, 38463, 24576));
}

// #22 review, finding 3: the ask worker still reads the capture buffer, so no new recording may start.
void test_a_running_ask_refuses_however_much_ram_is_free(){
	TEST_ASSERT_EQUAL(Verdict::AskStillRunning, VoiceTurnGate::check(true, IdleFree, IdleLargest));
	TEST_ASSERT_EQUAL(Verdict::AskStillRunning, VoiceTurnGate::check(true, 0, 0));
}

// A turn now takes up to 8,736 B from idle (17 Nov: 39,243 -> low 30,507). Below 32 KiB the margin is refused.
void test_less_than_the_guard_refuses(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 32767, IdleLargest));
}

void test_the_state_that_rebooted_in_20_is_refused(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 27907, 12288)); // turn5.log, turn 2
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, 27907, IdleLargest));
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, IdleFree, 12288));
}

// 17 Nov, turn7.log: after the Eight Ball routine the robot idled at 39,955 free with the largest block at 15,872,
// and the 16 KiB guard refused every question until a reboot ("my thoughts got lost").
void test_the_state_after_the_eight_ball_routine_may_listen(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, 39955, 15872));
}

void test_too_small_a_largest_block_refuses(){
	TEST_ASSERT_EQUAL(Verdict::LowInternalRam, VoiceTurnGate::check(false, IdleFree, 14335));
}

void test_exactly_the_thresholds_is_enough(){
	TEST_ASSERT_EQUAL(Verdict::Go, VoiceTurnGate::check(false, 32768, 14336));
}

}

void runVoiceTurnGateTests(){
	RUN_TEST(test_an_idle_robot_may_listen);
	RUN_TEST(test_a_connected_controller_or_a_past_rc_session_may_listen);
	RUN_TEST(test_a_running_ask_refuses_however_much_ram_is_free);
	RUN_TEST(test_less_than_the_guard_refuses);
	RUN_TEST(test_the_state_that_rebooted_in_20_is_refused);
	RUN_TEST(test_the_state_after_the_eight_ball_routine_may_listen);
	RUN_TEST(test_too_small_a_largest_block_refuses);
	RUN_TEST(test_exactly_the_thresholds_is_enough);
}
