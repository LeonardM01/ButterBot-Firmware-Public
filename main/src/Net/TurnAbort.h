#ifndef BUTTERBOT_FIRMWARE_TURNABORT_H
#define BUTTERBOT_FIRMWARE_TURNABORT_H

#include <atomic>
#include <cstdint>

/**
 * Sticky abort for one /v1/ask turn at a time (#9 point 4).
 *
 * The caller arms a turn before it starts preparing it (waiting for Wi-Fi, loading models,
 * filling the capture buffer). An abort() from any task at any point after that cancels the
 * turn, and the turn stays aborted however often it is checked. An abort() before arm() does
 * not touch the next turn. Lock-free, so abort() is safe from any task.
 *
 * No ESP-IDF dependencies: host-tested in test/host/ask_logic.
 */
class TurnAbort {
public:
	/** Starts a new turn and returns its id. Earlier turns are left as they are. */
	uint32_t arm(){
		return current.fetch_add(1) + 1;
	}

	/** Aborts the most recently armed turn. */
	void abort(){
		const uint32_t turn = current.load();
		uint32_t seen = abortedUpTo.load();
		// Only ever move forward, so a slow abort of an older turn can't undo a newer one.
		while(isBefore(seen, turn) && !abortedUpTo.compare_exchange_weak(seen, turn)){}
	}

	/** True once abort() was called while `turn` was the armed turn, or after it. */
	bool aborted(uint32_t turn) const{
		return !isBefore(abortedUpTo.load(), turn);
	}

private:
	std::atomic<uint32_t> current{0};
	std::atomic<uint32_t> abortedUpTo{0};

	/** Wrap-safe a < b for turn ids. */
	static bool isBefore(uint32_t a, uint32_t b){
		return (int32_t) (a - b) < 0;
	}
};

#endif //BUTTERBOT_FIRMWARE_TURNABORT_H
