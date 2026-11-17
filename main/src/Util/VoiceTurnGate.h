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
 * Idle internal RAM a turn is refused below (decisions #9), calibrated from question turns on the robot
 * (docs/voice-turn-memory.md, "Guard before the turn"). It can't certify a turn: it refuses one that would start
 * outside the range measured turns ran in.
 * - Free: on the 17 Nov bench a turn took at most 8,736 B from idle (39,243 -> low 30,507) over five turns; the
 *   dip to 3,019 B measured in #22 no longer happens since the phrase registration runs in the extmem window.
 *   32,768 (32 KiB) leaves such a turn about 24 KB, and sits 5,171 B under the lowest idle seen, with the
 *   controller connected over BLE (37,939; 38,463 after an RC session). It refuses #20's reboot state (27,907).
 * - Largest: the load carved up to 7,168 B out of the largest block (26,624 -> 19,456). 14,336 (14 KiB) is
 *   twice that. The Eight Ball routine loads the models outside this gate and can leave the largest block at
 *   15,872 (17 Nov, turn7.log); the old 16 KiB refused every question after it until a reboot. It refuses #20's 12,288.
 * Recalibrate from the `at=idle` and `int_min` lines when a turn runs with the controller connected, or when a
 * turn's demand changes.
 */
inline constexpr size_t MinIntFree = 32768;
inline constexpr size_t MinIntLargest = 14336;

enum class Verdict : uint8_t {
	Go,
	AskStillRunning, // the ask worker still reads the capture buffer (C7): no new recording may start
	LowInternalRam,  // below MinIntFree or MinIntLargest
};

/** `askWorkerBusy` is AskWorker::busy(); the sizes are the idle internal free size and largest free block. */
Verdict check(bool askWorkerBusy, size_t intFree, size_t intLargest);

}

#endif //BUTTERBOT_FIRMWARE_VOICETURNGATE_H
