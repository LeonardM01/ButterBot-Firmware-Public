#ifndef BUTTERBOT_FIRMWARE_TRANSITIONSLOT_H
#define BUTTERBOT_FIRMWARE_TRANSITIONSLOT_H

#include <atomic>

/**
 * The state change BBStateMachine will make on its next tick. No ESP-IDF dependencies: host-tested in
 * test/host/ask_logic.
 *
 * request() may come from any task (C7). take() hands the factory over and empties the slot in one step, before
 * the machine constructs the new state, so a state that asks to leave from its own constructor (ListenState
 * refusing the model load) stays pending for the next tick instead of being wiped.
 */
template<typename Factory>
class TransitionSlot {
public:
	/** Replaces any request not taken yet. */
	void request(Factory factory){ next.store(factory, std::memory_order_release); }

	bool pending() const{ return next.load(std::memory_order_acquire) != nullptr; }

	/** The pending factory, or nullptr; the slot is empty afterwards. */
	Factory take(){ return next.exchange(nullptr, std::memory_order_acq_rel); }

private:
	std::atomic<Factory> next{nullptr};
};

#endif //BUTTERBOT_FIRMWARE_TRANSITIONSLOT_H
