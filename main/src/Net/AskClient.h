#ifndef BUTTERBOT_FIRMWARE_ASKCLIENT_H
#define BUTTERBOT_FIRMWARE_ASKCLIENT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include "AskOutcome.h"
#include "TurnAbort.h"

class Audio;

class AudioGenerator;

/** Points in a turn where the AskClient's phase hook is called, for measurements. */
enum class AskPhase : uint8_t {
	Connected,  // connected (TCP, TLS) and request headers sent, before the upload
	Playing,    // playback just started; normally mid-download
	Downloaded, // last reply byte read, connection still open
};

/**
 * Optional hook on the ask() task at each AskPhase. Keep it short (no printf to the UART):
 * the time it takes counts towards the phase it interrupts.
 */
using AskPhaseHook = std::function<void(AskPhase phase)>;

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

/**
 * Client for butterbot-api's POST /v1/ask (#7, #8).
 *
 * Uploads a 16 kHz mono s16 WAV with the device bearer token and plays a 200 reply
 * while it downloads: the reply's WAV header is checked, then its PCM is streamed
 * through a PSRAM ring buffer into Audio. Outcomes come from the status code only;
 * the client logs the status, sizes and timings, never the token or a body.
 *
 * The API URL and token come from secrets.h. http:// is accepted only when
 * BB_DEV_INSECURE is 1 (checked at compile time), with a warning on every use.
 */
class AskClient {
public:
	/**
	 * Deadline for DNS + TCP (+ TLS) from the start of ask() (#7 point 8), and the timeout of each upload write.
	 *
	 * A connect that finishes after the deadline ends the turn as thoughts-lost. esp_http_client can't be
	 * interrupted mid-connect, though, so ask() can block past it: lwIP's resolver has no timeout parameter
	 * (about 7 s per DNS server when they don't answer), the TCP connect waits at most the budget left after
	 * DNS, and the TLS handshake checks that budget between handshake steps, each of which can wait as long
	 * again. Worst case is roughly DNS plus twice the budget left; aborts are noticed only after that.
	 */
	static constexpr uint32_t ConnectTimeoutMs = 5000;
	/** Upload done -> response headers (#7 point 8). */
	static constexpr uint32_t FirstByteTimeoutMs = 12000;

	AskClient(Audio& audio, AudioGenerator& generator, AskPhaseHook onPhase = {});

	/**
	 * Arms a new turn and returns its id. Call it before preparing the turn (waiting for Wi-Fi,
	 * loading models, recording), so an abortInFlight() at any point after this cancels it.
	 */
	static uint32_t beginTurn();

	/** True once the turn was aborted. Sticky: lets the caller skip or stop its own preparation. */
	static bool aborted(uint32_t turn);

	/**
	 * Runs one turn and returns once playback has finished, failed or was aborted.
	 *
	 * Blocking: run it on a worker task, not the state machine. The task needs about
	 * 8 KB of stack for esp_http_client and the TLS handshake; core 0 keeps it off
	 * the Audio and AFE tasks on core 1. Assumes the network is already up.
	 *
	 * @param turn from beginTurn(); if it was already aborted, ask() returns at once
	 * @param wav whole WAV file, RIFF header included; may live in flash or PSRAM
	 */
	AskResult ask(uint32_t turn, const uint8_t* wav, size_t len);

	/**
	 * Aborts the armed turn from any task, e.g. on RC entry or a button press. Noticed within
	 * about 0.5 s once ask() runs, except during DNS and connect (see ConnectTimeoutMs); playback stops too.
	 * A turn armed afterwards is not affected.
	 */
	static void abortInFlight();

	/** Prints a warning when BB_DEV_INSECURE is on. Call once at boot. */
	static void logSecurityPosture();

private:
	Audio& audio;
	AudioGenerator& generator;
	const AskPhaseHook onPhase;

	inline static TurnAbort turns;
};

#endif //BUTTERBOT_FIRMWARE_ASKCLIENT_H
