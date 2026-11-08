#include "UtteranceRecorder.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr size_t SampleBytes = 2;

constexpr size_t wholeSamples(size_t n){
	return n - (n % SampleBytes);
}
}

UtteranceRecorder::UtteranceRecorder(uint8_t* pcm, size_t capacity) : pcm(pcm), capacity(pcm == nullptr ? 0 : wholeSamples(capacity)){
}

void UtteranceRecorder::reset(){
	used = 0;
	current = State::Waiting;
	ending = End::None;
}

void UtteranceRecorder::onFetch(bool speech, std::span<const uint8_t> preRoll, std::span<const uint8_t> frame){
	switch(current){
		case State::Done:
			return;

		case State::Waiting:
			if(!speech) return;
			current = State::Speaking;
			append(preRoll);
			append(frame);
			return;

		case State::Speaking:
			if(!speech){
				finish(End::Silence);
				return;
			}
			append(frame);
			return;
	}
}

void UtteranceRecorder::append(std::span<const uint8_t> data){
	if(current != State::Speaking) return;

	// used <= capacity always holds, so the room cannot wrap (C4).
	const size_t n = std::min(wholeSamples(data.size()), capacity - used);
	if(n > 0){
		std::memcpy(pcm + used, data.data(), n);
		used += n;
	}
	if(used == capacity){
		finish(End::Cap);
	}
}

void UtteranceRecorder::finish(End why){
	current = State::Done;
	ending = why;
}
