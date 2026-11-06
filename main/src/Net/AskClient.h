#ifndef BUTTERBOT_FIRMWARE_ASKCLIENT_H
#define BUTTERBOT_FIRMWARE_ASKCLIENT_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "AskOutcome.h"

class Audio;

class AudioGenerator;

/** Points in a turn where AskConfig::onPhase is called, for measurements. */
enum class AskPhase : uint8_t {
	Connected,  // connected (DNS, TCP, TLS) and request headers sent, before the upload
	Playing,    // playback just started; normally mid-download
	Downloaded, // last reply byte read, connection still open
};

struct AskConfig {
	uint32_t connectTimeoutMs = 5000;    // DNS + TCP (+ TLS) and each upload write (#7 point 8)
	uint32_t firstByteTimeoutMs = 12000; // upload done -> response headers (#7 point 8)
	uint32_t stallTimeoutMs = 12000;     // longest gap between reply bytes once the reply has started
	size_t prebufferBytes = 16 * 1024;   // PCM buffered before playback starts (0.5 s)
	size_t minRingBytes = 64 * 1024;     // ring size when the reply length is unknown (2 s)
	size_t maxRingBytes = 640 * 1024;    // ring size cap: a 20 s reply (#7 point 5) fits without back-pressure
	size_t maxReplyPcmBytes = 30 * 32000; // PCM played at most (30 s); the server caps replies at ~20 s

	/**
	 * Optional hook on the ask() task at each AskPhase. Keep it short (no printf to the UART):
	 * the time it takes counts towards the download phase.
	 */
	void (*onPhase)(AskPhase phase, void* ctx) = nullptr;
	void* phaseCtx = nullptr;
};

/** One /v1/ask turn. Timings are in ms; 0 when the phase was not reached. */
struct AskResult {
	AskOutcome outcome = AskOutcome::ThoughtsLost;
	/** Set when abort() was called or another sound took over playback. Say nothing, whatever the outcome. */
	bool aborted = false;
	/** A 200 reply ended early (network error, stall, bad length). Some of it may have played. */
	bool truncated = false;
	int status = -1;            // HTTP status, -1 if none arrived

	uint32_t connectMs = 0;     // ask start -> connected and request headers sent
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
	AskClient(Audio& audio, AudioGenerator& generator, AskConfig config = {});

	/**
	 * Runs one turn and returns once playback has finished, failed or was aborted.
	 *
	 * Blocking: run it on a worker task, not the state machine. The task needs about
	 * 8 KB of stack for esp_http_client and the TLS handshake; core 0 keeps it off
	 * the Audio and AFE tasks on core 1. Assumes the network is already up.
	 *
	 * @param wav whole WAV file, RIFF header included; may live in flash or PSRAM
	 */
	AskResult ask(const uint8_t* wav, size_t len);

	/**
	 * Aborts the turn in flight from any task (e.g. on RC entry). Noticed within
	 * about 0.5 s, except during connect (up to connectTimeoutMs). Playback stops too.
	 * Has no effect on the next ask(), which clears it.
	 */
	void abort();

	/**
	 * Aborts whichever turn is in flight on any AskClient, like abort(). Called on RC entry,
	 * where the radio leaves the home network. A turn that starts afterwards is not affected.
	 */
	static void abortInFlight();

	/** Prints a warning when BB_DEV_INSECURE is on. Call once at boot. */
	static void logSecurityPosture();

private:
	Audio& audio;
	AudioGenerator& generator;
	const AskConfig config;
	std::atomic<bool> abortRequested{false};

	// abortInFlight() bumps the generation; a turn stops when it differs from the one it started with.
	inline static std::atomic<uint32_t> abortGeneration{0};
	uint32_t turnGeneration = 0;

	bool stopRequested() const;
	void notify(AskPhase phase) const;
};

#endif //BUTTERBOT_FIRMWARE_ASKCLIENT_H
