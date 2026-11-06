#ifndef BUTTERBOT_FIRMWARE_WAVHEADER_H
#define BUTTERBOT_FIRMWARE_WAVHEADER_H

#include <cstddef>
#include <cstdint>

/** Size of the canonical RIFF/WAVE header written in front of an uploaded question. */
inline constexpr size_t WavHeaderBytes = 44;

/**
 * Writes the canonical 44-byte header for `pcmBytes` of 16 kHz, mono, s16le PCM (#3), the only format
 * butterbot-api accepts. Refuses, writing nothing, when `pcmBytes` is odd (half a sample) or so large
 * that the RIFF size (36 + pcmBytes) would wrap (C4).
 */
bool writeWavHeader(uint8_t (&out)[WavHeaderBytes], uint32_t pcmBytes);

#endif //BUTTERBOT_FIRMWARE_WAVHEADER_H
