#include "WavHeader.h"
#include <cstring>

namespace {

constexpr uint32_t SampleRate = 16000;
constexpr uint16_t Channels = 1;
constexpr uint16_t BitsPerSample = 16;
constexpr uint16_t BlockAlign = Channels * BitsPerSample / 8;
constexpr uint32_t ByteRate = SampleRate * BlockAlign;
constexpr uint32_t RiffOverhead = WavHeaderBytes - 8; // "WAVE" + fmt chunk + data chunk header

void put16(uint8_t* p, uint16_t v){
	p[0] = (uint8_t) (v & 0xff);
	p[1] = (uint8_t) (v >> 8);
}

void put32(uint8_t* p, uint32_t v){
	for(int i = 0; i < 4; i++){
		p[i] = (uint8_t) ((v >> (8 * i)) & 0xff);
	}
}

}

bool writeWavHeader(uint8_t (&out)[WavHeaderBytes], uint32_t pcmBytes){
	if((pcmBytes % BlockAlign) != 0 || pcmBytes > UINT32_MAX - RiffOverhead){
		return false;
	}

	std::memcpy(out + 0, "RIFF", 4);
	put32(out + 4, RiffOverhead + pcmBytes);
	std::memcpy(out + 8, "WAVE", 4);
	std::memcpy(out + 12, "fmt ", 4);
	put32(out + 16, 16);         // fmt chunk size
	put16(out + 20, 1);          // PCM
	put16(out + 22, Channels);
	put32(out + 24, SampleRate);
	put32(out + 28, ByteRate);
	put16(out + 32, BlockAlign);
	put16(out + 34, BitsPerSample);
	std::memcpy(out + 36, "data", 4);
	put32(out + 40, pcmBytes);
	return true;
}
