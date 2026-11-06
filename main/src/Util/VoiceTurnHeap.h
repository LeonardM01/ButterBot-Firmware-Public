#ifndef BUTTERBOT_FIRMWARE_VOICETURNHEAP_H
#define BUTTERBOT_FIRMWARE_VOICETURNHEAP_H

#include <cstddef>
#include <cstdint>
#include "HeapSnapshot.h"

/**
 * Internal-RAM budget of a voice turn (docs/voice-turn-memory.md): the guard before the AFE + MultiNet load,
 * and one compact Info line per turn phase, tag `VoiceTurn`:
 *
 *   heap turn=N at=<idle|loaded|network|after> int_free= int_min= int_largest= psram_free= psram_largest=
 *
 * int_min is the lowest internal free size since this turn's idle line. The line carries sizes only, never
 * tokens, transcripts or reply text (S0.5). Called from the state machine task, except logOnline().
 */
namespace VoiceTurnHeap {

/**
 * The load needs this much internal RAM free and in one block, or ListenState refuses it (decisions #9).
 * Free: the largest load seen on the robot took 28,444 B (turn2.log, 28,703 -> low 259) and a load from
 * 27,907 B rebooted (turn5.log); 30,720 B leaves 2,276 B over that load and 2,813 B over the crash.
 * Largest: loads succeeded from a 7,680 B largest block (turn2.log turns 2-3), none were seen below it.
 */
inline constexpr size_t ModelLoadMinIntFree = 30720;
inline constexpr size_t ModelLoadMinIntLargest = 7680;

enum class Point : uint8_t {
	Idle,      // ListenState entered, before the guard
	Loaded,    // AFE + MultiNet loaded
	Network,   // connected to the API (taken on the ask worker at AskPhase::Connected)
	AfterTurn, // the turn is over, models unloaded, reply played
};

/** Counts a new turn, starts tracking its low point and logs the idle line. Returns the idle snapshot. */
HeapSnapshot beginTurn();

/** True when `idle` leaves the model load its measured margin (see ModelLoadMinIntFree). */
bool modelLoadAllowed(const HeapSnapshot& idle);

/** Logs a snapshot taken earlier at `point`; `not_reached` when it was never taken. */
void log(Point point, const HeapSnapshot& snapshot);

/** Takes a snapshot now and logs it. */
void mark(Point point);

/** Logs the after-turn line and stops tracking the low point. No-op when no turn is running. */
void endTurn();

/** One line the first time home Wi-Fi is up after boot, with the guard thresholds next to it. Any task. */
void logOnline();

}

#endif //BUTTERBOT_FIRMWARE_VOICETURNHEAP_H
