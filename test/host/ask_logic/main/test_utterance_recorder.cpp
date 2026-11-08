#include <unity.h>
#include <cstdint>
#include <span>
#include <vector>
#include "UtteranceRecorder.h"

using State = UtteranceRecorder::State;
using End = UtteranceRecorder::End;

// The AFE hands over 512 samples (32 ms, 1,024 B) per fetch.
static constexpr size_t Frame = 1024;
static constexpr size_t Cap = 320000; // 10 s of 16 kHz mono s16 (#3, decisions #2)

static std::vector<uint8_t> frame(uint8_t fill, size_t n = Frame){
	return std::vector<uint8_t>(n, fill);
}

static const std::span<const uint8_t> NoPreRoll{};

static void test_records_from_speech_onset_until_the_first_silence(){
	std::vector<uint8_t> buf(Cap, 0);
	UtteranceRecorder rec(buf.data(), buf.size());

	const auto silence = frame(0x00);
	const auto preRoll = frame(0x11, 3 * Frame); // VAD cache handed over at onset
	const auto speech = frame(0x22);

	rec.onFetch(false, NoPreRoll, silence); // before speech: not recorded
	rec.onFetch(false, NoPreRoll, silence);
	TEST_ASSERT_EQUAL(State::Waiting, rec.state());

	rec.onFetch(true, preRoll, speech);
	TEST_ASSERT_EQUAL(State::Speaking, rec.state());
	for(int i = 0; i < 9; i++) rec.onFetch(true, NoPreRoll, speech);
	TEST_ASSERT_EQUAL(4 * Frame + 9 * Frame, rec.bytes());

	rec.onFetch(false, NoPreRoll, silence); // the AFE reports silence only after ~1 s of it
	TEST_ASSERT_EQUAL(State::Done, rec.state());
	TEST_ASSERT_EQUAL(End::Silence, rec.end());
	TEST_ASSERT_EQUAL(13 * Frame, rec.bytes());

	// Pre-roll first, then the speech frames, in order.
	TEST_ASSERT_EACH_EQUAL_UINT8(0x11, buf.data(), 3 * Frame);
	TEST_ASSERT_EACH_EQUAL_UINT8(0x22, buf.data() + 3 * Frame, 10 * Frame);

	rec.onFetch(true, NoPreRoll, speech); // done is final
	TEST_ASSERT_EQUAL(13 * Frame, rec.bytes());
}

static void test_stops_at_exactly_the_10_s_cap(){
	std::vector<uint8_t> buf(Cap + 64, 0xee); // guard bytes past the cap
	UtteranceRecorder rec(buf.data(), Cap);
	const auto speech = frame(0x22);

	size_t fetches = 0;
	while(rec.state() != State::Done && fetches < 1000){
		rec.onFetch(true, NoPreRoll, speech);
		fetches++;
	}
	TEST_ASSERT_EQUAL(End::Cap, rec.end());
	TEST_ASSERT_EQUAL(Cap, rec.bytes());
	TEST_ASSERT_EQUAL(313u, fetches); // 312.5 frames: the last one is cut in half
	TEST_ASSERT_EACH_EQUAL_UINT8(0xee, buf.data() + Cap, 64);
}

static void test_the_pre_roll_counts_toward_the_cap(){
	std::vector<uint8_t> buf(8 * Frame, 0);
	UtteranceRecorder rec(buf.data(), buf.size());
	const auto preRoll = frame(0x11, 6 * Frame);
	const auto speech = frame(0x22);

	rec.onFetch(true, preRoll, speech);
	rec.onFetch(true, NoPreRoll, speech);
	TEST_ASSERT_EQUAL(State::Done, rec.state());
	TEST_ASSERT_EQUAL(End::Cap, rec.end());
	TEST_ASSERT_EQUAL(8 * Frame, rec.bytes());
}

static void test_a_pre_roll_larger_than_the_cap_is_cut(){
	std::vector<uint8_t> buf(2 * Frame, 0);
	UtteranceRecorder rec(buf.data(), buf.size());
	const auto preRoll = frame(0x11, 3 * Frame);
	rec.onFetch(true, preRoll, frame(0x22));
	TEST_ASSERT_EQUAL(End::Cap, rec.end());
	TEST_ASSERT_EQUAL(2 * Frame, rec.bytes());
	TEST_ASSERT_EACH_EQUAL_UINT8(0x11, buf.data(), 2 * Frame);
}

static void test_odd_byte_frames_keep_the_recording_in_whole_samples(){
	std::vector<uint8_t> buf(Cap, 0);
	UtteranceRecorder rec(buf.data(), buf.size());
	rec.onFetch(true, frame(0x11, 5), frame(0x22, Frame - 1));
	rec.onFetch(true, NoPreRoll, frame(0x33, 3));
	TEST_ASSERT_EQUAL(4u + (Frame - 2) + 2u, rec.bytes());
	TEST_ASSERT_EQUAL(0u, rec.bytes() % 2);
	TEST_ASSERT_EQUAL_HEX8(0x22, buf[4]);
	TEST_ASSERT_EQUAL_HEX8(0x33, buf[4 + Frame - 2]);
}

static void test_an_odd_capacity_is_rounded_down_to_whole_samples(){
	std::vector<uint8_t> buf(1001, 0);
	UtteranceRecorder rec(buf.data(), buf.size());
	rec.onFetch(true, NoPreRoll, frame(0x22));
	TEST_ASSERT_EQUAL(End::Cap, rec.end());
	TEST_ASSERT_EQUAL(1000u, rec.bytes());
}

static void test_no_speech_records_nothing(){
	std::vector<uint8_t> buf(Cap, 0);
	UtteranceRecorder rec(buf.data(), buf.size());
	for(int i = 0; i < 125; i++) rec.onFetch(false, NoPreRoll, frame(0x01)); // 4 s of silence
	TEST_ASSERT_EQUAL(State::Waiting, rec.state());
	TEST_ASSERT_EQUAL(End::None, rec.end());
	TEST_ASSERT_EQUAL(0u, rec.bytes());
	TEST_ASSERT_FALSE(rec.heardSpeech());
}

static void test_reset_starts_a_new_utterance_in_the_same_buffer(){
	std::vector<uint8_t> buf(4 * Frame, 0);
	UtteranceRecorder rec(buf.data(), buf.size());
	for(int i = 0; i < 5; i++) rec.onFetch(true, NoPreRoll, frame(0x22));
	TEST_ASSERT_EQUAL(State::Done, rec.state());

	rec.reset();
	TEST_ASSERT_EQUAL(State::Waiting, rec.state());
	TEST_ASSERT_EQUAL(End::None, rec.end());
	TEST_ASSERT_EQUAL(0u, rec.bytes());
	rec.onFetch(true, NoPreRoll, frame(0x44));
	TEST_ASSERT_EQUAL(Frame, rec.bytes());
	TEST_ASSERT_EQUAL_HEX8(0x44, buf[0]);
}

static void test_a_recorder_without_a_buffer_never_records(){
	UtteranceRecorder rec(nullptr, 0);
	rec.onFetch(true, NoPreRoll, frame(0x22));
	TEST_ASSERT_EQUAL(0u, rec.bytes());
	TEST_ASSERT_EQUAL(State::Done, rec.state());
	TEST_ASSERT_EQUAL(End::Cap, rec.end());
}

void runUtteranceRecorderTests(){
	RUN_TEST(test_records_from_speech_onset_until_the_first_silence);
	RUN_TEST(test_stops_at_exactly_the_10_s_cap);
	RUN_TEST(test_the_pre_roll_counts_toward_the_cap);
	RUN_TEST(test_a_pre_roll_larger_than_the_cap_is_cut);
	RUN_TEST(test_odd_byte_frames_keep_the_recording_in_whole_samples);
	RUN_TEST(test_an_odd_capacity_is_rounded_down_to_whole_samples);
	RUN_TEST(test_no_speech_records_nothing);
	RUN_TEST(test_reset_starts_a_new_utterance_in_the_same_buffer);
	RUN_TEST(test_a_recorder_without_a_buffer_never_records);
}
