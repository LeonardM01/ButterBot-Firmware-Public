#ifndef BUTTERBOT_FIRMWARE_ASKRESULT_H
#define BUTTERBOT_FIRMWARE_ASKRESULT_H

#include <cstdint>
#include "AskOutcome.h"

/** One /v1/ask turn. Timings are in ms; 0 when the phase was not reached. */
struct AskResult {
	AskOutcome outcome = AskOutcome::ThoughtsLost;
	/** Set when the turn was aborted or another sound took over playback. Say nothing, whatever the outcome. */
	bool aborted = false;
	/** A 200 reply ended early (network error, stall, bad length). Some of it may have played. */
	bool truncated = false;
	int status = -1;            // HTTP status, -1 if none arrived

	uint32_t dnsMs = 0;         // ask start -> API host resolved
	uint32_t connectMs = 0;     // host resolved -> connected (TCP, plus the TLS handshake on https) and headers sent
	uint32_t uploadBytes = 0;
	uint32_t uploadMs = 0;      // connected -> last body byte written
	uint32_t firstByteMs = 0;   // upload done -> response headers received
	uint32_t downloadBytes = 0; // reply body bytes, WAV header included
	uint32_t downloadMs = 0;    // first -> last reply body byte
	uint32_t firstAudioMs = 0;  // upload done -> playback started
	uint32_t underruns = 0;     // playback ran dry after it had started
	uint32_t starvedMs = 0;     // silence inserted by those underruns
	uint32_t pcmBytesPlayed = 0;
};

#endif //BUTTERBOT_FIRMWARE_ASKRESULT_H
