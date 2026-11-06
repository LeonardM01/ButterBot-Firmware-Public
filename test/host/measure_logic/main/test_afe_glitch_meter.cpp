#include <unity.h>
#include "AfeGlitchMeter.h"

namespace {
// The robot's numbers: 16 kHz, 512-frame (32 ms) feed chunks, 6 x 240-frame (90 ms) of I2S RX DMA buffers
// and a 50-chunk AFE ring.
constexpr AfeGlitchMeter::Config Robot{16000, 512, 1440, 50 * 512};
constexpr int64_t ChunkUs = 32000;

/** Reads `count` chunks at the mic's own pace, as when the feed task keeps up. */
int64_t readSteadily(AfeGlitchMeter& meter, int64_t t, int count){
	for(int i = 0; i < count; i++){
		t += ChunkUs;
		meter.onMicRead(t);
	}
	return t;
}
}

static void test_a_feed_task_that_keeps_up_loses_nothing(){
	AfeGlitchMeter meter(Robot);
	readSteadily(meter, 1000000, 100);
	const auto stats = meter.stats();
	TEST_ASSERT_EQUAL_UINT32(0, stats.micOverflows);
	TEST_ASSERT_EQUAL_UINT32(0, stats.micLostMs);
}

static void test_a_late_read_within_the_mic_queue_loses_nothing(){
	AfeGlitchMeter meter(Robot);
	int64_t t = readSteadily(meter, 1000000, 5);
	// 50 ms late: 82 ms of audio waiting (< 90 ms), 32 ms of it read now.
	t += ChunkUs + 50000;
	meter.onMicRead(t);
	// The backlog drains with reads that return at once.
	meter.onMicRead(t);
	meter.onMicRead(t);
	readSteadily(meter, t, 5);
	TEST_ASSERT_EQUAL_UINT32(0, meter.stats().micOverflows);
}

static void test_a_read_later_than_the_mic_queue_counts_an_overflow_and_the_lost_audio(){
	AfeGlitchMeter meter(Robot);
	int64_t t = readSteadily(meter, 1000000, 5);
	// 200 ms between reads: 3200 frames arrived, the buffers hold 1440, so 1760 frames (110 ms) were dropped.
	t += 200000;
	meter.onMicRead(t);
	readSteadily(meter, t, 5);
	const auto stats = meter.stats();
	TEST_ASSERT_EQUAL_UINT32(1, stats.micOverflows);
	TEST_ASSERT_EQUAL_UINT32(110, stats.micLostMs);
}

static void test_the_afe_backlog_is_what_was_fed_and_not_yet_fetched(){
	AfeGlitchMeter meter(Robot);
	int64_t t = 1000000;
	// The fetch task falls three chunks (96 ms) behind, then catches up.
	t = readSteadily(meter, t, 3);
	for(int i = 0; i < 3; i++) meter.onFetch(512);
	t = readSteadily(meter, t, 1);
	meter.onFetch(512);
	const auto stats = meter.stats();
	TEST_ASSERT_EQUAL_UINT32(96, stats.afeBacklogMaxMs);
	TEST_ASSERT_EQUAL_UINT32(0, stats.afeOverflows);
}

static void test_a_fetch_task_further_behind_than_the_afe_ring_counts_an_overflow(){
	AfeGlitchMeter meter(Robot);
	readSteadily(meter, 1000000, 51); // 51 chunks fed, none fetched; the ring holds 50
	const auto stats = meter.stats();
	TEST_ASSERT_EQUAL_UINT32(1, stats.afeOverflows);
	TEST_ASSERT_EQUAL_UINT32(1600, stats.afeBacklogMaxMs); // 50 chunks of 32 ms
}

static void test_failed_reads_and_fetches_are_counted(){
	AfeGlitchMeter meter(Robot);
	meter.onMicReadError();
	meter.onFetchError();
	meter.onFetchError();
	const auto stats = meter.stats();
	TEST_ASSERT_EQUAL_UINT32(1, stats.micReadErrors);
	TEST_ASSERT_EQUAL_UINT32(2, stats.fetchErrors);
}

static void test_reset_starts_from_zero_and_ignores_the_gap_before_it(){
	AfeGlitchMeter meter(Robot);
	int64_t t = readSteadily(meter, 1000000, 52);
	t += 500000;
	meter.onMicRead(t);
	meter.onMicReadError();
	meter.onFetchError();

	meter.reset();
	// The first read after a reset (e.g. a turn starting long after the last one) is not late.
	readSteadily(meter, t + 10000000, 3);
	meter.onFetch(512);
	const auto stats = meter.stats();
	TEST_ASSERT_EQUAL_UINT32(0, stats.micOverflows);
	TEST_ASSERT_EQUAL_UINT32(0, stats.micLostMs);
	TEST_ASSERT_EQUAL_UINT32(0, stats.micReadErrors);
	TEST_ASSERT_EQUAL_UINT32(0, stats.fetchErrors);
	TEST_ASSERT_EQUAL_UINT32(0, stats.afeOverflows);
	TEST_ASSERT_EQUAL_UINT32(96, stats.afeBacklogMaxMs);
}

void runAfeGlitchMeterTests(){
	RUN_TEST(test_a_feed_task_that_keeps_up_loses_nothing);
	RUN_TEST(test_a_late_read_within_the_mic_queue_loses_nothing);
	RUN_TEST(test_a_read_later_than_the_mic_queue_counts_an_overflow_and_the_lost_audio);
	RUN_TEST(test_the_afe_backlog_is_what_was_fed_and_not_yet_fetched);
	RUN_TEST(test_a_fetch_task_further_behind_than_the_afe_ring_counts_an_overflow);
	RUN_TEST(test_failed_reads_and_fetches_are_counted);
	RUN_TEST(test_reset_starts_from_zero_and_ignores_the_gap_before_it);
}
