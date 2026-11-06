#ifndef BUTTERBOT_FIRMWARE_ASKLINES_H
#define BUTTERBOT_FIRMWARE_ASKLINES_H

#include <cstdint>
#include "AskResult.h"

/** What the robot says once a voice question has been through /v1/ask (#10, #22). */
enum class AskLine : uint8_t {
	None,            // the reply played, or the turn was cancelled (the abort phrase was already said)
	Offline,         // no home Wi-Fi when the question was handed over
	DidntUnderstand, // 204: spoken with the shared Phrase::ListeningTimeout
	ThoughtsLost,    // any other status, a timeout, a transport error or a reply cut short
};

/**
 * The line for a finished turn. `online` is WiFiService::isOnline() at hand-over (decisions #7): a DNS or
 * connect failure mid-turn is ThoughtsLost, not Offline.
 */
AskLine lineFor(bool online, const AskResult& result);

/**
 * In-character text for Offline and ThoughtsLost, spoken through flite. `pick` selects a variant (any value;
 * it wraps). nullptr for None and DidntUnderstand, which have no local text. These stay local to the voice
 * turn and never enter the shared Phrase enum that travels over BLE (decisions #10).
 */
const char* askLineText(AskLine line, uint32_t pick);

/** The "thinking out loud" filler said while the question uploads. `pick` wraps. */
const char* askFillerText(uint32_t pick);

#endif //BUTTERBOT_FIRMWARE_ASKLINES_H
