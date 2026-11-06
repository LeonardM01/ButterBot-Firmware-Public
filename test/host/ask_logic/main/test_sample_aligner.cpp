#include <unity.h>
#include <cstdint>
#include <vector>
#include "SampleAligner.h"

// Collects what the aligner releases, in order, as the ring buffer would see it.
static std::vector<uint8_t> push(SampleAligner& a, std::vector<uint8_t> in, std::vector<size_t>* spanLens = nullptr){
	const auto out = a.push(in.data(), in.size());
	std::vector<uint8_t> got;
	got.insert(got.end(), out.pair, out.pair + out.pairLen);
	got.insert(got.end(), out.body, out.body + out.bodyLen);
	if(spanLens){
		spanLens->push_back(out.pairLen);
		spanLens->push_back(out.bodyLen);
	}
	return got;
}

static void test_even_chunks_pass_through_unchanged(){
	SampleAligner a;
	std::vector<size_t> lens;
	TEST_ASSERT_TRUE((std::vector<uint8_t>{1, 2, 3, 4}) == push(a, {1, 2, 3, 4}, &lens));
	TEST_ASSERT_EQUAL_size_t(0, lens[0]);
	TEST_ASSERT_FALSE(a.hasCarry());
}

static void test_an_odd_trailing_byte_waits_for_its_partner(){
	SampleAligner a;
	TEST_ASSERT_TRUE((std::vector<uint8_t>{1, 2}) == push(a, {1, 2, 3}));
	TEST_ASSERT_TRUE(a.hasCarry());
	TEST_ASSERT_TRUE((std::vector<uint8_t>{3, 4, 5, 6}) == push(a, {4, 5, 6}));
	TEST_ASSERT_FALSE(a.hasCarry());
}

static void test_completing_a_pair_and_holding_a_new_byte_keeps_both_intact(){
	SampleAligner a;
	push(a, {1, 2, 3});
	TEST_ASSERT_TRUE((std::vector<uint8_t>{3, 4}) == push(a, {4, 5}));
	TEST_ASSERT_TRUE((std::vector<uint8_t>{5, 6}) == push(a, {6}));
}

static void test_every_released_span_is_whole_samples(){
	SampleAligner a;
	std::vector<size_t> lens;
	push(a, {1}, &lens);
	push(a, {2, 3, 4}, &lens);
	push(a, {5, 6, 7, 8, 9}, &lens);
	push(a, {}, &lens);
	for(size_t len : lens){
		TEST_ASSERT_EQUAL_size_t(0, len % 2);
	}
}

static void test_single_bytes_pair_up(){
	SampleAligner a;
	TEST_ASSERT_TRUE(push(a, {7}).empty());
	TEST_ASSERT_TRUE((std::vector<uint8_t>{7, 8}) == push(a, {8}));
	TEST_ASSERT_TRUE(push(a, {}).empty());
}

void runSampleAlignerTests(){
	RUN_TEST(test_even_chunks_pass_through_unchanged);
	RUN_TEST(test_an_odd_trailing_byte_waits_for_its_partner);
	RUN_TEST(test_completing_a_pair_and_holding_a_new_byte_keeps_both_intact);
	RUN_TEST(test_every_released_span_is_whole_samples);
	RUN_TEST(test_single_bytes_pair_up);
}
