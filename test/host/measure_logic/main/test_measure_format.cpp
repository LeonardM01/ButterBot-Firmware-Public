#include <unity.h>
#include <cstring>
#include "MeasureFormat.h"

static void test_throughput_is_kib_per_second_with_one_decimal(){
	char buf[16];
	// 68,480 B (the canned question) in 120 ms = 557.29 KiB/s.
	formatKiBps(buf, sizeof(buf), 68480, 120);
	TEST_ASSERT_EQUAL_STRING("557.3", buf);
	formatKiBps(buf, sizeof(buf), 1024, 1000);
	TEST_ASSERT_EQUAL_STRING("1.0", buf);
	formatKiBps(buf, sizeof(buf), 0, 250);
	TEST_ASSERT_EQUAL_STRING("0.0", buf);
}

static void test_throughput_without_a_duration_is_a_dash(){
	// A phase not reached, or one shorter than a millisecond, has no rate.
	char buf[16];
	formatKiBps(buf, sizeof(buf), 4096, 0);
	TEST_ASSERT_EQUAL_STRING("-", buf);
}

static void test_throughput_does_not_overflow_on_a_large_fast_transfer(){
	char buf[16];
	// 640 KiB in 1 ms.
	formatKiBps(buf, sizeof(buf), 655360, 1);
	TEST_ASSERT_EQUAL_STRING("640000.0", buf);
}

static void test_the_ask_line_carries_every_measurement(){
	AskResult r;
	r.outcome = AskOutcome::Play;
	r.status = 200;
	r.dnsMs = 12;
	r.connectMs = 35;
	r.uploadBytes = 68480;
	r.uploadMs = 120;
	r.firstByteMs = 1850;
	r.downloadBytes = 160044;
	r.downloadMs = 400;
	r.firstAudioMs = 1900;
	r.underruns = 2;
	r.starvedMs = 64;
	r.pcmBytesPlayed = 160000;

	char buf[MeasureLineBytes];
	formatAskMeasure(buf, sizeof(buf), r);
	TEST_ASSERT_EQUAL_STRING("BBMEASURE ask outcome=play status=200 aborted=0 truncated=0 dns_ms=12 connect_ms=35 "
							 "up_bytes=68480 up_ms=120 up_KiB_s=557.3 first_byte_ms=1850 "
							 "down_bytes=160044 down_ms=400 down_KiB_s=390.7 first_audio_ms=1900 "
							 "underruns=2 starved_ms=64 played_bytes=160000", buf);
}

static void test_a_failed_turn_names_its_outcome_and_flags(){
	AskResult r; // defaults: thoughts lost, no status, nothing reached
	r.aborted = true;
	r.truncated = true;

	char buf[MeasureLineBytes];
	formatAskMeasure(buf, sizeof(buf), r);
	TEST_ASSERT_EQUAL_STRING("BBMEASURE ask outcome=lost status=-1 aborted=1 truncated=1 dns_ms=0 connect_ms=0 "
							 "up_bytes=0 up_ms=0 up_KiB_s=- first_byte_ms=0 "
							 "down_bytes=0 down_ms=0 down_KiB_s=- first_audio_ms=0 "
							 "underruns=0 starved_ms=0 played_bytes=0", buf);

	r.outcome = AskOutcome::DidntUnderstand;
	formatAskMeasure(buf, sizeof(buf), r);
	TEST_ASSERT_EQUAL(0, strncmp("BBMEASURE ask outcome=didnt_understand ", buf, 39));
}

static void test_a_short_buffer_is_cut_and_terminated(){
	AskResult r;
	char buf[24];
	memset(buf, 'x', sizeof(buf));
	formatAskMeasure(buf, sizeof(buf), r);
	TEST_ASSERT_EQUAL_STRING("BBMEASURE ask outcome=l", buf);
}

static void test_the_afe_line_carries_every_glitch_counter(){
	AfeGlitchMeter::Stats stats{1, 110, 2, 96, 3, 4};
	char buf[MeasureLineBytes];
	formatAfeMeasure(buf, sizeof(buf), stats);
	TEST_ASSERT_EQUAL_STRING("BBMEASURE afe mic_overflows=1 mic_lost_ms=110 mic_read_errors=2 "
							 "afe_backlog_max_ms=96 afe_overflows=3 fetch_errors=4", buf);
}

static void test_the_stack_line_names_the_task_and_its_high_water_mark(){
	char buf[MeasureLineBytes];
	// The ask worker over http on the robot: 9,532 B of 12,288 never touched.
	formatStackMeasure(buf, sizeof(buf), "AskWorker", 12288, 9532);
	TEST_ASSERT_EQUAL_STRING("BBMEASURE stack task=AskWorker size=12288 min_free=9532", buf);
}

void runMeasureFormatTests(){
	RUN_TEST(test_throughput_is_kib_per_second_with_one_decimal);
	RUN_TEST(test_throughput_without_a_duration_is_a_dash);
	RUN_TEST(test_throughput_does_not_overflow_on_a_large_fast_transfer);
	RUN_TEST(test_the_ask_line_carries_every_measurement);
	RUN_TEST(test_a_failed_turn_names_its_outcome_and_flags);
	RUN_TEST(test_a_short_buffer_is_cut_and_terminated);
	RUN_TEST(test_the_afe_line_carries_every_glitch_counter);
	RUN_TEST(test_the_stack_line_names_the_task_and_its_high_water_mark);
}
