#include "PcmPassthroughGen.h"

void PcmPassthroughGen::open(std::unique_ptr<AudioSource> source){
	source->open();
	this->source = std::move(source);
}

void PcmPassthroughGen::close(){
	if(source == nullptr) return;
	source->close();
	source.reset();
}

size_t PcmPassthroughGen::getData(uint8_t* buffer, size_t bytes){
	if(source == nullptr) return 0;

	size_t filled = 0;
	while(filled < bytes && (bool) *source){
		const size_t n = source->getData(buffer + filled, bytes - filled);
		if(n == 0) break;
		filled += n;
	}
	return filled;
}
