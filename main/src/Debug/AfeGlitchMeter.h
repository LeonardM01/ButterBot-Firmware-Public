#ifndef BUTTERBOT_FIRMWARE_AFEGLITCHMETER_H
#define BUTTERBOT_FIRMWARE_AFEGLITCHMETER_H

#include <atomic>
#include <cstdint>

/**
 * Counts audio-input glitches during the measurement build's ask turn (#4, #20): audio the mic
 * dropped because the AFE feed task read it late, and audio the AFE had to hold because its
 * fetch task fell behind. Both happen when Wi-Fi, TLS or BLE crowd the AFE's tasks.
 *
 * Mic: the I2S RX driver keeps a few DMA buffers and drops the oldest when all are full.
 * The meter models that queue from the time between reads: what arrived since the last read
 * goes in, each read takes out a chunk, and whatever overflows the queue is lost. That is an
 * estimate, good to about one DMA buffer (15 ms), and needs no hook in the driver.
 *
 * Call onMicRead() from the feed task only and the fetch hooks from the fetch task only; stats()
 * from any task. reset() only while both tasks are stopped.
 *
 * No ESP-IDF dependencies: host-tested in test/host/measure_logic.
 */
class AfeGlitchMeter {
public:
	struct Config {
		uint32_t sampleRate;     // frames per second
		uint32_t framesPerRead;  // frames per channel in one mic read (one AFE feed chunk)
		uint32_t micQueueFrames; // frames the I2S RX DMA buffers hold before the driver drops the oldest
		uint32_t afeRingFrames;  // frames the AFE buffers between feed and fetch
	};

	struct Stats {
		uint32_t micOverflows;    // reads that came after the mic's DMA buffers had overflowed
		uint32_t micLostMs;       // audio those overflows dropped
		uint32_t micReadErrors;   // mic reads that failed or came back short (that chunk is not fed)
		uint32_t afeBacklogMaxMs; // most audio fed to the AFE and not yet fetched
		uint32_t afeOverflows;    // feeds that found the AFE's ring already full
		uint32_t fetchErrors;     // fetches that returned nothing or failed (including the fetch timeout)
	};

	explicit AfeGlitchMeter(const Config& config);

	/** Starts counting from zero. */
	void reset();

	/** A mic read of framesPerRead frames returned at `nowUs`; they are fed to the AFE next. */
	void onMicRead(int64_t nowUs);

	void onMicReadError();

	/** The AFE handed out `frames` processed frames. */
	void onFetch(uint32_t frames);

	void onFetchError();

	Stats stats() const;

private:
	Config config;

	// Feed task only.
	int64_t lastReadUs = 0;
	uint32_t micBacklog = 0; // frames waiting in the I2S queue after the last read

	std::atomic<uint32_t> micOverflows{0};
	std::atomic<uint32_t> micLostFrames{0};
	std::atomic<uint32_t> micReadErrors{0};
	std::atomic<uint32_t> fed{0};     // frames, feed task
	std::atomic<uint32_t> fetched{0}; // frames, fetch task
	std::atomic<uint32_t> afeBacklogMax{0};
	std::atomic<uint32_t> afeOverflows{0};
	std::atomic<uint32_t> fetchErrors{0};

	void countMicArrivals(int64_t nowUs);

	void countFeed();

	uint32_t framesToMs(uint32_t frames) const;
};

#endif //BUTTERBOT_FIRMWARE_AFEGLITCHMETER_H
