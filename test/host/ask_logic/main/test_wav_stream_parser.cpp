#include <unity.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "WavStreamParser.h"

using State = WavStreamParser::State;

// The first 48 bytes of butterbot-api's question.wav fixture: a canonical 44-byte
// header (16 kHz, mono, s16, data = 68,436 B) followed by two PCM samples.
static const uint8_t QuestionWav[] = {
		'R', 'I', 'F', 'F', 0x78, 0x0b, 0x01, 0x00, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 0x10, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x01, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x7d, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00,
		'd', 'a', 't', 'a', 0x54, 0x0b, 0x01, 0x00,
		0xe2, 0xff, 0xd6, 0xff,
};
static constexpr size_t QuestionHeaderBytes = 44;

static void put16(std::vector<uint8_t>& v, uint16_t x){
	v.push_back(x & 0xff);
	v.push_back(x >> 8);
}

static void put32(std::vector<uint8_t>& v, uint32_t x){
	for(int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xff);
}

static void putId(std::vector<uint8_t>& v, const char* id){
	v.insert(v.end(), id, id + 4);
}

static void putRiff(std::vector<uint8_t>& v){
	putId(v, "RIFF");
	put32(v, 0); // the RIFF size is not trusted, so any value works
	putId(v, "WAVE");
}

static void putFmt(std::vector<uint8_t>& v, uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits, uint32_t chunkSize = 16){
	putId(v, "fmt ");
	put32(v, chunkSize);
	put16(v, format);
	put16(v, channels);
	put32(v, rate);
	put32(v, rate * channels * bits / 8);
	put16(v, channels * bits / 8);
	put16(v, bits);
	for(uint32_t i = 16; i < chunkSize; i++) v.push_back(0);
}

static void putChunk(std::vector<uint8_t>& v, const char* id, uint32_t size){
	putId(v, id);
	put32(v, size);
	for(uint32_t i = 0; i < size + size % 2; i++) v.push_back(0xAA);
}

static void putDataHeader(std::vector<uint8_t>& v, uint32_t size){
	putId(v, "data");
	put32(v, size);
}

static State feedAll(WavStreamParser& p, const std::vector<uint8_t>& v){
	return p.feed(v.data(), v.size()).state;
}

static void test_canonical_header_in_one_chunk_starts_pcm_after_44_bytes(){
	WavStreamParser p;
	const auto r = p.feed(QuestionWav, sizeof(QuestionWav));
	TEST_ASSERT_EQUAL(State::Pcm, r.state);
	TEST_ASSERT_EQUAL_size_t(QuestionHeaderBytes, r.headerBytes);
	TEST_ASSERT_TRUE(p.dataBytes().has_value());
	TEST_ASSERT_EQUAL_UINT32(68436, *p.dataBytes());
}

static void test_header_split_into_single_bytes_is_parsed(){
	WavStreamParser p;
	for(size_t i = 0; i < QuestionHeaderBytes - 1; i++){
		const auto r = p.feed(&QuestionWav[i], 1);
		TEST_ASSERT_EQUAL(State::NeedMore, r.state);
		TEST_ASSERT_EQUAL_size_t(1, r.headerBytes);
	}
	const auto r = p.feed(&QuestionWav[QuestionHeaderBytes - 1], 5);
	TEST_ASSERT_EQUAL(State::Pcm, r.state);
	TEST_ASSERT_EQUAL_size_t(1, r.headerBytes); // the other 4 bytes are PCM
}

static void test_after_the_header_everything_is_pcm(){
	WavStreamParser p;
	p.feed(QuestionWav, QuestionHeaderBytes);
	const uint8_t more[] = {'R', 'I', 'F', 'F'};
	const auto r = p.feed(more, sizeof(more));
	TEST_ASSERT_EQUAL(State::Pcm, r.state);
	TEST_ASSERT_EQUAL_size_t(0, r.headerBytes);
}

static void test_wrong_rate_channels_bits_or_format_is_invalid(){
	struct { uint16_t format, channels; uint32_t rate; uint16_t bits; } cases[] = {
			{1, 1, 24000, 16}, {1, 1, 44100, 16}, {1, 2, 16000, 16}, {1, 1, 16000, 8}, {3, 1, 16000, 32}, {0xFFFE, 1, 16000, 16},
	};
	for(const auto& c : cases){
		std::vector<uint8_t> v;
		putRiff(v);
		putFmt(v, c.format, c.channels, c.rate, c.bits);
		putDataHeader(v, 64);
		WavStreamParser p;
		TEST_ASSERT_EQUAL(State::Invalid, feedAll(p, v));
	}
}

static void test_non_riff_or_non_wave_is_invalid(){
	std::vector<uint8_t> notRiff(QuestionWav, QuestionWav + sizeof(QuestionWav));
	notRiff[0] = 'X';
	WavStreamParser a;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(a, notRiff));

	std::vector<uint8_t> notWave(QuestionWav, QuestionWav + sizeof(QuestionWav));
	notWave[8] = 'A';
	WavStreamParser b;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(b, notWave));
}

static void test_html_error_page_is_invalid_without_waiting_for_more(){
	const char* html = "<html><body>502 Bad Gateway</body></html>";
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Invalid, p.feed(reinterpret_cast<const uint8_t*>(html), 4).state);
}

static void test_unknown_chunks_are_skipped_including_the_pad_byte(){
	std::vector<uint8_t> v;
	putRiff(v);
	putChunk(v, "JUNK", 3); // odd size, followed by a pad byte
	putFmt(v, 1, 1, 16000, 16);
	putChunk(v, "LIST", 26);
	putDataHeader(v, 100);
	const size_t header = v.size();
	v.push_back(0x11);
	v.push_back(0x22);

	WavStreamParser p;
	const auto r = p.feed(v.data(), v.size());
	TEST_ASSERT_EQUAL(State::Pcm, r.state);
	TEST_ASSERT_EQUAL_size_t(header, r.headerBytes);
	TEST_ASSERT_EQUAL_UINT32(100, *p.dataBytes());
}

static void test_extended_fmt_chunk_is_accepted(){
	std::vector<uint8_t> v;
	putRiff(v);
	putFmt(v, 1, 1, 16000, 16, 18); // WAVEFORMATEX with cbSize = 0
	putDataHeader(v, 2);
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Pcm, feedAll(p, v));
}

static void test_short_fmt_chunk_is_invalid(){
	std::vector<uint8_t> v;
	putRiff(v);
	putChunk(v, "fmt ", 14);
	putDataHeader(v, 2);
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(p, v));
}

static void test_data_before_fmt_is_invalid(){
	std::vector<uint8_t> v;
	putRiff(v);
	putDataHeader(v, 2);
	v.push_back(0);
	v.push_back(0);
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(p, v));
}

static void test_odd_data_size_is_invalid(){
	std::vector<uint8_t> v;
	putRiff(v);
	putFmt(v, 1, 1, 16000, 16);
	putDataHeader(v, 101);
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(p, v));
}

static void test_streaming_placeholder_data_sizes_mean_unknown_length(){
	for(uint32_t size : {0u, 0xFFFFFFFFu}){
		std::vector<uint8_t> v;
		putRiff(v);
		putFmt(v, 1, 1, 16000, 16);
		putDataHeader(v, size);
		WavStreamParser p;
		TEST_ASSERT_EQUAL(State::Pcm, feedAll(p, v));
		TEST_ASSERT_FALSE(p.dataBytes().has_value());
	}
}

static void test_header_longer_than_the_limit_is_invalid(){
	std::vector<uint8_t> v;
	putRiff(v);
	putFmt(v, 1, 1, 16000, 16);
	putChunk(v, "LIST", WavStreamParser::MaxHeaderBytes);
	putDataHeader(v, 2);
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(p, v));
}

static void test_huge_declared_chunk_is_invalid_before_it_is_downloaded(){
	std::vector<uint8_t> v;
	putRiff(v);
	putFmt(v, 1, 1, 16000, 16);
	putId(v, "LIST");
	put32(v, 0x7FFFFFFF);
	WavStreamParser p;
	TEST_ASSERT_EQUAL(State::Invalid, feedAll(p, v));
}

static void test_invalid_is_sticky(){
	WavStreamParser p;
	const uint8_t junk[] = {'J', 'U', 'N', 'K'};
	p.feed(junk, sizeof(junk));
	const auto r = p.feed(QuestionWav, sizeof(QuestionWav));
	TEST_ASSERT_EQUAL(State::Invalid, r.state);
	TEST_ASSERT_EQUAL(State::Invalid, p.state());
}

void runWavStreamParserTests(){
	RUN_TEST(test_canonical_header_in_one_chunk_starts_pcm_after_44_bytes);
	RUN_TEST(test_header_split_into_single_bytes_is_parsed);
	RUN_TEST(test_after_the_header_everything_is_pcm);
	RUN_TEST(test_wrong_rate_channels_bits_or_format_is_invalid);
	RUN_TEST(test_non_riff_or_non_wave_is_invalid);
	RUN_TEST(test_html_error_page_is_invalid_without_waiting_for_more);
	RUN_TEST(test_unknown_chunks_are_skipped_including_the_pad_byte);
	RUN_TEST(test_extended_fmt_chunk_is_accepted);
	RUN_TEST(test_short_fmt_chunk_is_invalid);
	RUN_TEST(test_data_before_fmt_is_invalid);
	RUN_TEST(test_odd_data_size_is_invalid);
	RUN_TEST(test_streaming_placeholder_data_sizes_mean_unknown_length);
	RUN_TEST(test_header_longer_than_the_limit_is_invalid);
	RUN_TEST(test_huge_declared_chunk_is_invalid_before_it_is_downloaded);
	RUN_TEST(test_invalid_is_sticky);
}
