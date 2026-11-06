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
 * Idle internal RAM a turn is refused below (decisions #9), calibrated from four question turns on the robot
 * (docs/voice-turn-memory.md, "Guard before the model load"). It can't certify a turn: it refuses one that
 * would start outside the range those turns ran in.
 * - Free: a turn took up to 38,116 B from idle (41,135 -> low 3,019). 38,912 (38 KiB) leaves such a turn
 *   796 B even if that demand is fixed, and sits 1,823 B under the lowest idle seen (40,735; idle varied by
 *   400 B). It refuses #20's reboot state (27,907 free).
 * - Largest: the load carved up to 7,168 B out of the largest block (26,624 -> 19,456). 16,384 (16 KiB) is
 *   over twice that and 9,216 B under the lowest idle largest (25,600). It refuses #20's 12,288.
 * Recalibrate from the `at=idle` and `int_min` lines once local commands, cancels, RC and a connected phone
 * have been measured.
 */
inline constexpr size_t MinIntFree = 38912;
inline constexpr size_t MinIntLargest = 16384;

enum class Verdict : uint8_t {
	Go,
	AskStillRunning, // the ask worker still reads the capture buffer (C7): no new recording may start
	LowInternalRam,  // below MinIntFree or MinIntLargest
};

/** `askWorkerBusy` is AskWorker::busy(); the sizes are the idle internal free size and largest free block. */
Verdict check(bool askWorkerBusy, size_t intFree, size_t intLargest);

}

#endif //BUTTERBOT_FIRMWARE_VOICETURNGATE_H
