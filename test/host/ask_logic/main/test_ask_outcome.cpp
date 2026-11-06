#include <unity.h>
#include "AskOutcome.h"

static void test_200_plays_the_reply(){
	TEST_ASSERT_EQUAL(AskOutcome::Play, outcomeForStatus(200));
}

static void test_204_means_the_robot_did_not_understand(){
	TEST_ASSERT_EQUAL(AskOutcome::DidntUnderstand, outcomeForStatus(204));
}

static void test_rejections_and_server_failures_lose_the_thought(){
	const int statuses[] = {400, 401, 413, 415, 429, 500, 502, 503, 504};
	for(int status : statuses){
		TEST_ASSERT_EQUAL_MESSAGE(AskOutcome::ThoughtsLost, outcomeForStatus(status), "server status");
	}
}

static void test_other_success_and_redirect_codes_lose_the_thought(){
	// Only 200 carries audio and only 204 means "no speech". Redirects are never followed.
	const int statuses[] = {201, 202, 206, 301, 302, 304, 307, 308, 100};
	for(int status : statuses){
		TEST_ASSERT_EQUAL_MESSAGE(AskOutcome::ThoughtsLost, outcomeForStatus(status), "unexpected status");
	}
}

static void test_transport_errors_and_timeouts_lose_the_thought(){
	// esp_http_client reports -1 when no status line arrived (timeout, reset, DNS or TLS failure).
	TEST_ASSERT_EQUAL(AskOutcome::ThoughtsLost, outcomeForStatus(-1));
	TEST_ASSERT_EQUAL(AskOutcome::ThoughtsLost, outcomeForStatus(0));
}

void runAskOutcomeTests(){
	RUN_TEST(test_200_plays_the_reply);
	RUN_TEST(test_204_means_the_robot_did_not_understand);
	RUN_TEST(test_rejections_and_server_failures_lose_the_thought);
	RUN_TEST(test_other_success_and_redirect_codes_lose_the_thought);
	RUN_TEST(test_transport_errors_and_timeouts_lose_the_thought);
}
