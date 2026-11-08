#ifndef BUTTERBOT_FIRMWARE_VOICECAPTURE_H
#define BUTTERBOT_FIRMWARE_VOICECAPTURE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "UtteranceRecorder.h"
#include "Net/WavHeader.h"

/**
 * The one buffer a spoken question is recorded into and uploaded from (#22): a 44 B WAV header followed by
 * at most 10 s of 16 kHz mono s16 PCM, 320,044 B in PSRAM (M6).
 *
 * Allocated once at boot and reused by every turn (M8), so a turn never asks the heap for it. Ownership by
 * phase (C7): the AFE proc task writes through recorder() while ListenState has it attached to the
 * AudioFrontend; once the AFE is Off, the state machine task reads it (wav()), and the ask worker reads the
 * returned bytes until its turn ends.
 */
class VoiceCapture {
public:
	static constexpr size_t PcmCapBytes = 320000;
	static constexpr size_t BufferBytes = WavHeaderBytes + PcmCapBytes;

	/**
	 * Allocates the buffer in PSRAM. nullptr if PSRAM is short; with CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y
	 * the failed allocation reboots instead (M11), which at boot with ~8 MB of PSRAM free does not happen.
	 */
	static std::unique_ptr<VoiceCapture> create();

	/** Starts a new recording; the previous one is gone. Only while nothing feeds the recorder. */
	void begin();

	UtteranceRecorder& recorder(){ return rec; }

	/**
	 * Writes the header for what was recorded and returns the whole WAV, header included. Empty when nothing
	 * was recorded. Only once the recorder is no longer fed.
	 */
	std::span<const uint8_t> wav();

	/** The raw buffer, BufferBytes long, for the measurement build's canned question. */
	std::span<uint8_t> raw(){ return {buf.get(), BufferBytes}; }

	struct CapsFree {
		void operator()(uint8_t* p) const;
	};

private:
	explicit VoiceCapture(std::unique_ptr<uint8_t, CapsFree> buf);

	std::unique_ptr<uint8_t, CapsFree> buf;
	UtteranceRecorder rec;
};

#endif //BUTTERBOT_FIRMWARE_VOICECAPTURE_H
