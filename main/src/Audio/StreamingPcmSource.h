#ifndef BUTTERBOT_FIRMWARE_STREAMINGPCMSOURCE_H
#define BUTTERBOT_FIRMWARE_STREAMINGPCMSOURCE_H

#include <Services/Audio/AudioSource.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

/**
 * PCM that arrives while it plays: a byte ring buffer in PSRAM between a producer
 * task (the ask client) and the Audio task.
 *
 * Shared by both sides through a shared_ptr, because Audio owns and destroys the
 * StreamingPcmSource whenever playback stops, including when another sound replaces it.
 *
 * Audio stops playback as soon as a source returns 0 bytes, so while the producer
 * is behind the source plays silence instead and counts an underrun.
 */
class PcmStream {
public:
	struct Stats {
		uint32_t underruns;   // times playback ran dry after real audio had started
		uint32_t starvedMs;   // total silence inserted because of underruns
		uint32_t bytesPlayed; // PCM bytes handed to Audio (excludes inserted silence)
	};

	/** Ring buffer of `capacity` bytes in PSRAM. nullptr if PSRAM is exhausted. */
	static std::shared_ptr<PcmStream> create(size_t capacity);

	~PcmStream();

	PcmStream(const PcmStream&) = delete;

	PcmStream& operator=(const PcmStream&) = delete;

	// Producer side.

	/**
	 * Appends whole 16-bit samples (len must be even). Blocks up to `wait` for room,
	 * which back-pressures the network when playback is slower than the download.
	 * @return false on timeout, or when the consumer is gone or the stream was cancelled
	 */
	bool write(const uint8_t* data, size_t len, TickType_t wait);

	/** No more data will be written. Playback ends once the buffer drains. */
	void finish();

	/** Stops playback promptly: the source reports its end at the next read, dropping buffered audio. */
	void cancel();

	size_t buffered() const;

	/** The source was closed by Audio: playback ended, was stopped, or was replaced by another sound. */
	bool consumerGone() const;

	Stats stats() const;

private:
	friend class StreamingPcmSource;

	explicit PcmStream(RingbufHandle_t ring);

	RingbufHandle_t ring;
	std::atomic<size_t> bufferedBytes{0};
	std::atomic<bool> finished{false};
	std::atomic<bool> cancelled{false};
	std::atomic<bool> gone{false};
	std::atomic<uint32_t> underruns{0};
	std::atomic<uint32_t> starvedUs{0};
	std::atomic<uint32_t> bytesPlayed{0};

	size_t read(uint8_t* buffer, size_t bytes, TickType_t wait);

	bool ended() const;
};

/**
 * AudioSource end of a PcmStream. Never returns 0 before the stream has ended:
 * it waits at most ReadWaitMs for data, then returns silence for the whole request.
 */
class StreamingPcmSource : public AudioSource {
public:
	explicit StreamingPcmSource(std::shared_ptr<PcmStream> stream);

	~StreamingPcmSource() override;

	void open() override;

	void close() override;

	size_t getData(uint8_t* buffer, size_t bytes) override;

	operator bool() const override;

private:
	/** Bounded so Audio::stop()/play() from other tasks, which wait for the audio tick, are not held up. */
	static constexpr uint32_t ReadWaitMs = 10;

	std::shared_ptr<PcmStream> stream;
	bool started = false;    // real audio has been played
	bool starving = false;   // currently filling silence
	int64_t starveStartUs = 0;

	/** Adds the current run of inserted silence, if any, to the stream's starved time. */
	void endStarvation();
};

#endif //BUTTERBOT_FIRMWARE_STREAMINGPCMSOURCE_H
