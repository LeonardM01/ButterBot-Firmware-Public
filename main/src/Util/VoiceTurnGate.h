#ifndef BUTTERBOT_FIRMWARE_VOICETURNGATE_H
#define BUTTERBOT_FIRMWARE_VOICETURNGATE_H

#include <cstddef>
#include <cstdint>

/**
 * Whether ListenState may start a voice turn: record into the capture buffer and load AFE + MultiNet.
 * No ESP-IDF dependencies: host-tested in test/host/ask_logic.
 */
namespace VoiceTurnGate {

/**
 * Idle internal RAM the model load is refused below (decisions #9). A safety net, not a proven margin: see
 * docs/voice-turn-memory.md, "Guard before the model load". Free: above every idle value a #20 load started
 * from (27,268-28,703 B), the crash at 27,907 B included. Largest: the lowest block a #20 load succeeded from.
 * Calibrate both from the robot's `at=idle` / `at=loaded int_min` lines.
 */
inline constexpr size_t MinIntFree = 30720;
inline constexpr size_t MinIntLargest = 7680;

enum class Verdict : uint8_t {
	Go,
	AskStillRunning, // the ask worker still reads the capture buffer (C7): no new recording may start
	LowInternalRam,  // below MinIntFree or MinIntLargest
};

/** `askWorkerBusy` is AskWorker::busy(); the sizes are the idle internal free size and largest free block. */
Verdict check(bool askWorkerBusy, size_t intFree, size_t intLargest);

}

#endif //BUTTERBOT_FIRMWARE_VOICETURNGATE_H
