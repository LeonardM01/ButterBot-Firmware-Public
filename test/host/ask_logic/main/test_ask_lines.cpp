#include <unity.h>
#include <cstring>
#include "AskLines.h"

// A finished turn as AskClient reports it: the outcome comes from the status alone (AskOutcome),
// and the flags from how the exchange ended.
static AskResult finished(int status, bool truncated = false, bool aborted = false){
	AskResult r;
	r.status = status;
	r.outcome = outcomeForStatus(status);
	r.truncated = truncated;
	r.aborted = aborted;
	return r;
}

static void test_a_complete_reply_needs_no_line(){
	TEST_ASSERT_EQUAL(AskLine::None, lineFor(true, finished(200)));
}

static void test_a_reply_cut_short_loses_the_thought(){
	// AskClient sets the outcome to ThoughtsLost too; the flag alone must be enough.
	AskResult r = finished(200, true);
	TEST_ASSERT_EQUAL(AskLine::ThoughtsLost, lineFor(true, r));
	r.outcome = AskOutcome::ThoughtsLost;
	TEST_ASSERT_EQUAL(AskLine::ThoughtsLost, lineFor(true, r));
}

static void test_204_says_it_did_not_understand(){
	TEST_ASSERT_EQUAL(AskLine::DidntUnderstand, lineFor(true, finished(204)));
}

static void test_client_and_server_errors_lose_the_thought(){
	const int statuses[] = {400, 401, 413, 415, 429, 500, 502, 503, 504};
	for(int status : statuses){
		TEST_ASSERT_EQUAL_MESSAGE(AskLine::ThoughtsLost, lineFor(true, finished(status)), "HTTP error status");
	}
}

static void test_no_status_or_a_timeout_loses_the_thought(){
	// -1: DNS, connect (5 s) or first-byte (12 s) timeout, or a transport error before the status line.
	TEST_ASSERT_EQUAL(AskLine::ThoughtsLost, lineFor(true, finished(-1)));
	TEST_ASSERT_EQUAL(AskLine::ThoughtsLost, lineFor(true, AskResult{}));
}

static void test_an_aborted_turn_says_nothing_more(){
	// The cancel path already spoke the abort phrase, whatever the exchange got to.
	TEST_ASSERT_EQUAL(AskLine::None, lineFor(true, finished(-1, false, true)));
	TEST_ASSERT_EQUAL(AskLine::None, lineFor(true, finished(200, true, true)));
	TEST_ASSERT_EQUAL(AskLine::None, lineFor(true, finished(204, false, true)));
}

static void test_offline_has_its_own_line(){
	TEST_ASSERT_EQUAL(AskLine::Offline, lineFor(false, AskResult{}));
	TEST_ASSERT_EQUAL(AskLine::Offline, lineFor(false, finished(200)));
}

static void test_offline_and_lost_lines_are_distinct_and_never_empty(){
	for(uint32_t pick = 0; pick < 8; pick++){
		const char* offline = askLineText(AskLine::Offline, pick);
		const char* lost = askLineText(AskLine::ThoughtsLost, pick);
		const char* filler = askFillerText(pick);
		TEST_ASSERT_NOT_NULL(offline);
		TEST_ASSERT_NOT_NULL(lost);
		TEST_ASSERT_NOT_NULL(filler);
		TEST_ASSERT_TRUE(std::strlen(offline) > 0 && std::strlen(lost) > 0 && std::strlen(filler) > 0);
		for(uint32_t other = 0; other < 8; other++){
			TEST_ASSERT_TRUE_MESSAGE(std::strcmp(offline, askLineText(AskLine::ThoughtsLost, other)) != 0, "offline line reused as lost");
		}
	}
}

static void test_lines_spoken_from_shared_phrases_have_no_local_text(){
	// DidntUnderstand reuses Phrase::ListeningTimeout; None says nothing.
	TEST_ASSERT_NULL(askLineText(AskLine::DidntUnderstand, 0));
	TEST_ASSERT_NULL(askLineText(AskLine::None, 0));
}

void runAskLinesTests(){
	RUN_TEST(test_a_complete_reply_needs_no_line);
	RUN_TEST(test_a_reply_cut_short_loses_the_thought);
	RUN_TEST(test_204_says_it_did_not_understand);
	RUN_TEST(test_client_and_server_errors_lose_the_thought);
	RUN_TEST(test_no_status_or_a_timeout_loses_the_thought);
	RUN_TEST(test_an_aborted_turn_says_nothing_more);
	RUN_TEST(test_offline_has_its_own_line);
	RUN_TEST(test_offline_and_lost_lines_are_distinct_and_never_empty);
	RUN_TEST(test_lines_spoken_from_shared_phrases_have_no_local_text);
}
