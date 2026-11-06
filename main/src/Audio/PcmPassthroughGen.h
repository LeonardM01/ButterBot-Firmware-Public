#ifndef BUTTERBOT_FIRMWARE_PCMPASSTHROUGHGEN_H
#define BUTTERBOT_FIRMWARE_PCMPASSTHROUGHGEN_H

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

private:
	std::unique_ptr<AudioSource> source;
};

#endif //BUTTERBOT_FIRMWARE_PCMPASSTHROUGHGEN_H
