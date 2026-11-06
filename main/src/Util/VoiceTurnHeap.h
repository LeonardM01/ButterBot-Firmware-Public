#ifndef BUTTERBOT_FIRMWARE_VOICETURNHEAP_H
#define BUTTERBOT_FIRMWARE_VOICETURNHEAP_H

#include <cstddef>
#include <cstdint>
#include "HeapSnapshot.h"

/**
 * Internal-RAM budget of a voice turn (docs/voice-turn-memory.md): one compact Info line per turn phase, tag
 * `VoiceTurn`. The guard before the AFE + MultiNet load is VoiceTurnGate.
 *
 *   heap turn=N at=<idle|loaded|listening|unloaded|network|playing|downloaded|after> int_free= int_min= ...
 *
 * int_min is the lowest internal free size since this turn's idle line, so the first line whose int_min drops
 * places a dip in the sub-phase that line closes. The line carries sizes only, never
 * tokens, transcripts or reply text (S0.5). Called from the state machine task, except logOnline().
 */
namespace VoiceTurnHeap {

enum class Point : uint8_t {
	Idle,       // ListenState entered, before the guard
	Loaded,     // AFE + MultiNet loaded (the intro is playing)
	Listening,  // phrases registered, intro played, Speech mode on
	Unloaded,   // recognition and recording over, models freed; before any closing line or filler
	Network,    // DNS + connect done, filler synthesising (taken on the ask worker at AskPhase::Connected)
	Playing,    // upload done, reply streaming and queued behind the filler (AskPhase::Playing)
	Downloaded, // last reply byte read (AskPhase::Downloaded)
	AfterTurn,  // the turn is over, models unloaded, reply played
};

/** Counts a new turn, starts tracking its low point and logs the idle line. Returns the idle snapshot. */
HeapSnapshot beginTurn();

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
