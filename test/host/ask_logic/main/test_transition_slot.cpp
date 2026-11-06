#include <unity.h>
#include "TransitionSlot.h"

namespace {

using Factory = int (*)();

int enterListen(){ return 1; }

int enterIdle(){ return 2; }

void test_take_returns_the_request_and_empties_the_slot(){
	TransitionSlot<Factory> slot;
	TEST_ASSERT_FALSE(slot.pending());

	slot.request(&enterListen);
	TEST_ASSERT_TRUE(slot.pending());
	TEST_ASSERT_EQUAL_PTR(&enterListen, slot.take());
	TEST_ASSERT_FALSE(slot.pending());
	TEST_ASSERT_NULL(slot.take());
}

// ListenState's refusal (#22 review, finding 1): the new state asks to leave from its own constructor,
// which runs after the machine took its factory. That request must still be there on the next tick.
void test_a_request_made_while_the_taken_state_is_built_stays_pending(){
	TransitionSlot<Factory> slot;
	slot.request(&enterListen);

	const Factory building = slot.take();
	TEST_ASSERT_EQUAL_PTR(&enterListen, building);
	slot.request(&enterIdle); // from inside the constructor

	TEST_ASSERT_TRUE(slot.pending());
	TEST_ASSERT_EQUAL_PTR(&enterIdle, slot.take());
}

void test_the_last_request_before_a_take_wins(){
	TransitionSlot<Factory> slot;
	slot.request(&enterListen);
	slot.request(&enterIdle);
	TEST_ASSERT_EQUAL_PTR(&enterIdle, slot.take());
}

}

void runTransitionSlotTests(){
	RUN_TEST(test_take_returns_the_request_and_empties_the_slot);
	RUN_TEST(test_a_request_made_while_the_taken_state_is_built_stays_pending);
	RUN_TEST(test_the_last_request_before_a_take_wins);
}
