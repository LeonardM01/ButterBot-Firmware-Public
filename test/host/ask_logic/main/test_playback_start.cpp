#include <unity.h>
#include "PlaybackStart.h"

namespace {

using PlaybackStart::ready;

// Turn 3 of the 17 Nov bench log: 204,856 B of reply PCM downloaded in 22,121 ms (about 9.3 KB/s), which
// played with 34 underruns (15.2 s of silence) behind the old fixed 0.5 s prebuffer.
constexpr size_t SlowReplyBytes = 204856;
constexpr uint32_t SlowDownloadMs = 22121;

uint32_t elapsedAt(size_t received, size_t replyBytes, uint32_t downloadMs){
	return (uint32_t) ((uint64_t) received * downloadMs / replyBytes);
}

void test_a_fast_link_starts_after_one_second_of_audio(){
	// Turn 2: 148,536 B in 5,424 ms would be ready well before; here the link is faster still.
	TEST_ASSERT_TRUE(ready(PlaybackStart::MinBytes, 148536, 300));
}

void test_less_than_one_second_of_audio_never_starts_a_long_reply(){
	TEST_ASSERT_FALSE(ready(PlaybackStart::MinBytes - 2, 148536, 10));
	TEST_ASSERT_FALSE(ready(16 * 1024, 148536, 1));
}

void test_a_slow_link_waits_until_the_rest_will_arrive_in_time(){
	TEST_ASSERT_FALSE(ready(100000, SlowReplyBytes, elapsedAt(100000, SlowReplyBytes, SlowDownloadMs)));
	TEST_ASSERT_TRUE(ready(170000, SlowReplyBytes, elapsedAt(170000, SlowReplyBytes, SlowDownloadMs)));
}

void test_a_complete_reply_starts_however_short(){
	TEST_ASSERT_TRUE(ready(8000, 8000, 5));
	TEST_ASSERT_TRUE(ready(SlowReplyBytes, SlowReplyBytes, SlowDownloadMs));
}

void test_an_unknown_length_starts_after_one_second_of_audio(){
	TEST_ASSERT_FALSE(ready(PlaybackStart::MinBytes - 2, 0, 5000));
	TEST_ASSERT_TRUE(ready(PlaybackStart::MinBytes, 0, 5000));
}

void test_no_rate_measured_yet_waits(){
	TEST_ASSERT_FALSE(ready(PlaybackStart::MinBytes, 148536, 0));
}

void test_huge_values_do_not_wrap(){
	// C4: a 30 s reply after more than an hour of download must not overflow into "ready".
	TEST_ASSERT_FALSE(ready(PlaybackStart::MinBytes, 960000, 4000000));
	TEST_ASSERT_FALSE(ready(PlaybackStart::MinBytes, 960000, UINT32_MAX)); // rest x elapsed near 2^52
	TEST_ASSERT_FALSE(ready(PlaybackStart::MinBytes, UINT32_MAX, UINT32_MAX)); // near 2^64
}

// The symptom: at a steady download rate, playback started by ready() never catches up with the download.
void test_at_a_steady_rate_playback_never_runs_dry(){
	constexpr size_t Chunk = 2048; // AskClient's read size
	for(uint32_t rate = 2000; rate <= 200000; rate += 1000){ // bytes per second
		size_t startAt = SlowReplyBytes;
		for(size_t received = Chunk; received < SlowReplyBytes; received += Chunk){
			if(ready(received, SlowReplyBytes, (uint32_t) ((uint64_t) received * 1000 / rate))){
				startAt = received;
				break;
			}
		}
		const double tStart = (double) startAt / rate;
		// Byte x is downloaded at x / rate and played at tStart + x / BytesPerSecond.
		for(size_t x = startAt; x <= SlowReplyBytes; x += Chunk){
			const double downloaded = (double) x / rate;
			const double played = tStart + (double) x / PlaybackStart::BytesPerSecond;
			if(downloaded > played){
				TEST_FAIL_MESSAGE("playback overtook the download");
			}
		}
	}
}

}

void runPlaybackStartTests(){
	RUN_TEST(test_a_fast_link_starts_after_one_second_of_audio);
	RUN_TEST(test_less_than_one_second_of_audio_never_starts_a_long_reply);
	RUN_TEST(test_a_slow_link_waits_until_the_rest_will_arrive_in_time);
	RUN_TEST(test_a_complete_reply_starts_however_short);
	RUN_TEST(test_an_unknown_length_starts_after_one_second_of_audio);
	RUN_TEST(test_no_rate_measured_yet_waits);
	RUN_TEST(test_huge_values_do_not_wrap);
	RUN_TEST(test_at_a_steady_rate_playback_never_runs_dry);
}
