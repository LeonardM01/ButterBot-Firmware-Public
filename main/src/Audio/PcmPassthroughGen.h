#ifndef BUTTERBOT_FIRMWARE_PCMPASSTHROUGHGEN_H
#define BUTTERBOT_FIRMWARE_PCMPASSTHROUGHGEN_H

#include <atomic>
#include <memory>
#include "Services/Audio/AudioGenerator.h"

/**
 * Plays a 16 kHz mono s16 AudioSource as-is: no compressor, reverb or decoding.
 *
 * For cloud replies, whose voice is already finished speech, and for measurements, where
 * SpeechAudioGen's DSP on the Audio task would blur underrun numbers.
 */
class PcmPassthroughGen : public AudioGenerator {
public:
	void open(std::unique_ptr<AudioSource> source) override;

	void close() override;

	size_t getData(uint8_t* buffer, size_t bytes) override;

	/**
	 * True while Audio plays a source on this generator, from open() to close(). A source queued behind
	 * another sound is opened only when that sound ends, so this is when it becomes audible. Any task.
	 */
	bool isOpen() const{ return opened.load(std::memory_order_acquire); }

private:
	std::unique_ptr<AudioSource> source;
	std::atomic<bool> opened{false}; // written on the Audio task
};

#endif //BUTTERBOT_FIRMWARE_PCMPASSTHROUGHGEN_H
