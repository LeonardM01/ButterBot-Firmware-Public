#include <unity.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "WavHeader.h"
#include "WavStreamParser.h"

using State = WavStreamParser::State;

// butterbot-api's question.wav fixture header (16 kHz, mono, s16, data = 68,436 B), byte for byte.
static const uint8_t QuestionHeader[44] = {
		'R', 'I', 'F', 'F', 0x78, 0x0b, 0x01, 0x00, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 0x10, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x01, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x7d, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00,
		'd', 'a', 't', 'a', 0x54, 0x0b, 0x01, 0x00,
};

static void test_header_matches_a_known_good_file(){
	uint8_t out[WavHeaderBytes];
	TEST_ASSERT_TRUE(writeWavHeader(out, 68436));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(QuestionHeader, out, sizeof(out));
}

static void test_header_round_trips_through_the_parser(){
	const uint32_t sizes[] = {0, 2, 1024, 68436, 320000};
	for(uint32_t pcm : sizes){
		uint8_t hdr[WavHeaderBytes];
		TEST_ASSERT_TRUE(writeWavHeader(hdr, pcm));
		WavStreamParser parser;
		const auto r = parser.feed(hdr, sizeof(hdr));
		TEST_ASSERT_EQUAL(State::Pcm, r.state);
		TEST_ASSERT_EQUAL(WavHeaderBytes, r.headerBytes);
		if(pcm == 0){
			// 0 means "unknown length" to a streaming parser; the server rejects an empty upload anyway.
			TEST_ASSERT_FALSE(parser.dataBytes().has_value());
		}else{
			TEST_ASSERT_TRUE(parser.dataBytes().has_value());
			TEST_ASSERT_EQUAL_UINT32(pcm, *parser.dataBytes());
		}
	}
}

static void test_a_full_capture_buffer_parses_as_header_plus_pcm(){
	// The 10 s cap: 44 B header + 320,000 B PCM = the 320,044 B capture buffer.
	std::vector<uint8_t> buf(WavHeaderBytes + 320000, 0x5a);
	uint8_t hdr[WavHeaderBytes];
	TEST_ASSERT_TRUE(writeWavHeader(hdr, 320000));
	std::memcpy(buf.data(), hdr, sizeof(hdr));
	TEST_ASSERT_EQUAL(320044u, buf.size());

	WavStreamParser parser;
	size_t pcmSeen = 0;
	for(size_t off = 0; off < buf.size(); off += 4096){
		const size_t n = std::min<size_t>(4096, buf.size() - off);
		size_t skip = 0;
		if(parser.state() != State::Pcm){
			const auto r = parser.feed(buf.data() + off, n);
			TEST_ASSERT_NOT_EQUAL(State::Invalid, r.state);
			skip = r.headerBytes;
		}
		pcmSeen += n - skip;
	}
	TEST_ASSERT_EQUAL(State::Pcm, parser.state());
	TEST_ASSERT_EQUAL(320000u, pcmSeen);
}

static void test_a_tampered_header_is_rejected(){
	uint8_t hdr[WavHeaderBytes];
	TEST_ASSERT_TRUE(writeWavHeader(hdr, 1024));
	hdr[24] = 0x44; // sample rate 16000 -> 16068
	WavStreamParser parser;
	TEST_ASSERT_EQUAL(State::Invalid, parser.feed(hdr, sizeof(hdr)).state);
}

static void test_odd_or_oversized_pcm_is_refused(){
	uint8_t hdr[WavHeaderBytes];
	std::memset(hdr, 0xee, sizeof(hdr));
	TEST_ASSERT_FALSE(writeWavHeader(hdr, 1023));         // half a sample
	TEST_ASSERT_FALSE(writeWavHeader(hdr, 0xFFFFFFFEu));  // RIFF size 36 + n would wrap
	TEST_ASSERT_FALSE(writeWavHeader(hdr, 0xFFFFFFDCu));  // exactly 2^32 - 36: still too big
	TEST_ASSERT_EQUAL_HEX8(0xee, hdr[0]);                 // nothing written on refusal
	TEST_ASSERT_TRUE(writeWavHeader(hdr, 0xFFFFFFDAu));   // the largest even size that fits
}

void runWavHeaderTests(){
	RUN_TEST(test_header_matches_a_known_good_file);
	RUN_TEST(test_header_round_trips_through_the_parser);
	RUN_TEST(test_a_full_capture_buffer_parses_as_header_plus_pcm);
	RUN_TEST(test_a_tampered_header_is_rejected);
	RUN_TEST(test_odd_or_oversized_pcm_is_refused);
}
