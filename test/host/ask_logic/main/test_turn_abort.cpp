#include <unity.h>
#include "TurnAbort.h"

static void test_an_abort_while_the_turn_is_prepared_cancels_it(){
	TurnAbort turns;
	const uint32_t turn = turns.arm(); // armed before the models load
	turns.abort();                     // e.g. RC entry while the caller is still preparing
	TEST_ASSERT_TRUE(turns.aborted(turn));
	TEST_ASSERT_TRUE(turns.aborted(turn)); // sticky: checking does not clear it
}

static void test_an_armed_turn_runs_until_aborted(){
	TurnAbort turns;
	const uint32_t turn = turns.arm();
	TEST_ASSERT_FALSE(turns.aborted(turn));
}

static void test_an_abort_before_the_turn_is_armed_does_not_affect_it(){
	TurnAbort turns;
	turns.abort(); // nothing armed yet
	const uint32_t first = turns.arm();
	TEST_ASSERT_FALSE(turns.aborted(first));

	turns.abort();
	const uint32_t second = turns.arm();
	TEST_ASSERT_TRUE(turns.aborted(first));
	TEST_ASSERT_FALSE(turns.aborted(second));
}

static void test_repeated_aborts_keep_the_turn_aborted(){
	TurnAbort turns;
	const uint32_t turn = turns.arm();
	turns.abort();
	turns.abort();
	TEST_ASSERT_TRUE(turns.aborted(turn));
}

void runTurnAbortTests(){
	RUN_TEST(test_an_abort_while_the_turn_is_prepared_cancels_it);
	RUN_TEST(test_an_armed_turn_runs_until_aborted);
	RUN_TEST(test_an_abort_before_the_turn_is_armed_does_not_affect_it);
	RUN_TEST(test_repeated_aborts_keep_the_turn_aborted);
}
