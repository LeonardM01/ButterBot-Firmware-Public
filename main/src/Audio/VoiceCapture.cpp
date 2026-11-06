#include "VoiceCapture.h"
#include <esp_heap_caps.h>

void VoiceCapture::CapsFree::operator()(uint8_t* p) const{
	heap_caps_free(p);
}

std::unique_ptr<VoiceCapture> VoiceCapture::create(){
	std::unique_ptr<uint8_t, CapsFree> buf(static_cast<uint8_t*>(heap_caps_malloc(BufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
	if(!buf){
		return nullptr;
	}
	return std::unique_ptr<VoiceCapture>(new VoiceCapture(std::move(buf)));
}

VoiceCapture::VoiceCapture(std::unique_ptr<uint8_t, CapsFree> buffer)
		: buf(std::move(buffer)), rec(buf.get() + WavHeaderBytes, PcmCapBytes){
}

void VoiceCapture::begin(){
	rec.reset();
}

std::span<const uint8_t> VoiceCapture::wav(){
	const size_t pcm = rec.bytes(); // <= PcmCapBytes, even
	if(pcm == 0){
		return {};
	}
	uint8_t (&header)[WavHeaderBytes] = *reinterpret_cast<uint8_t (*)[WavHeaderBytes]>(buf.get());
	if(!writeWavHeader(header, static_cast<uint32_t>(pcm))){
		return {};
	}
	return {buf.get(), WavHeaderBytes + pcm};
}
