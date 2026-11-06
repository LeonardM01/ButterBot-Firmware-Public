#ifndef BUTTERBOT_FIRMWARE_WAVSTREAMPARSER_H
#define BUTTERBOT_FIRMWARE_WAVSTREAMPARSER_H

#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * Incremental RIFF/WAVE header check for a reply that arrives in arbitrary chunks.
 *
 * Accepts only PCM, 16 kHz, mono, 16-bit (#7 point 4), mirroring butterbot-api's
 * checkWAV. Unknown chunks before "data" are skipped (word-aligned). Unlike the
 * server it never needs the whole body: it stops at the "data" chunk header and
 * everything after that is PCM. No allocation; the header is never buffered whole.
 */
class WavStreamParser {
public:
	/** Headers longer than this are rejected rather than downloaded and skipped. */
	static constexpr size_t MaxHeaderBytes = 1024;

	enum class State {
		NeedMore, // header not complete yet, feed more bytes
		Pcm,      // header accepted, the rest of the stream is PCM
		Invalid,  // not a 16 kHz mono s16 PCM WAV (sticky)
	};

	struct Result {
		State state;
		/** Bytes of this input that belonged to the header. When state is Pcm, the remaining len - headerBytes are PCM. */
		size_t headerBytes;
	};

	Result feed(const uint8_t* data, size_t len);

	State state() const;

	/** Size of the data chunk, or nullopt when unknown (0 or 0xFFFFFFFF, as streaming encoders write). Valid once state is Pcm. */
	std::optional<uint32_t> dataBytes() const;

private:
	enum class Phase { Riff, ChunkHeader, Fmt, Skip, Done };

	State current = State::NeedMore;
	Phase phase = Phase::Riff;
	uint8_t scratch[16] = {};
	size_t have = 0;       // bytes in scratch
	uint32_t skip = 0;     // bytes left to skip in the current chunk
	size_t consumed = 0;   // header bytes seen so far
	bool sawFmt = false;
	uint32_t dataSize = 0;

	State fail();

	/** Copies up to `want` bytes into scratch. Returns true once scratch holds `want` bytes. */
	bool collect(const uint8_t*& p, size_t& left, size_t want);

	/** Called when an 8-byte chunk header is in scratch. */
	void onChunkHeader();

	void onFmt();
};

#endif //BUTTERBOT_FIRMWARE_WAVSTREAMPARSER_H
