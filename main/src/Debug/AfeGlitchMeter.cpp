#include "AfeGlitchMeter.h"

AfeGlitchMeter::AfeGlitchMeter(const Config& config) : config(config){
}

void AfeGlitchMeter::reset(){
	lastReadUs = 0;
	micBacklog = 0;
	micOverflows = 0;
	micLostFrames = 0;
	micReadErrors = 0;
	fed = 0;
	fetched = 0;
	afeBacklogMax = 0;
	afeOverflows = 0;
	fetchErrors = 0;
}

void AfeGlitchMeter::onMicRead(int64_t nowUs){
	countMicArrivals(nowUs);
	countFeed();
}

void AfeGlitchMeter::onMicReadError(){
	micReadErrors++;
}

void AfeGlitchMeter::onFetch(uint32_t frames){
	fetched += frames;
}

void AfeGlitchMeter::onFetchError(){
	fetchErrors++;
}

AfeGlitchMeter::Stats AfeGlitchMeter::stats() const{
	return {micOverflows, framesToMs(micLostFrames), micReadErrors, framesToMs(afeBacklogMax), afeOverflows, fetchErrors};
}

void AfeGlitchMeter::countMicArrivals(int64_t nowUs){
	if(lastReadUs != 0 && nowUs > lastReadUs){
		const uint64_t arrived = (uint64_t) (nowUs - lastReadUs) * config.sampleRate / 1000000;
		uint64_t waiting = micBacklog + arrived;
		if(waiting > config.micQueueFrames){
			micOverflows++;
			micLostFrames += (uint32_t) (waiting - config.micQueueFrames);
			waiting = config.micQueueFrames;
		}
		micBacklog = waiting > config.framesPerRead ? (uint32_t) (waiting - config.framesPerRead) : 0;
	}
	lastReadUs = nowUs;
}

void AfeGlitchMeter::countFeed(){
	fed += config.framesPerRead;
	const int32_t backlog = (int32_t) (fed.load() - fetched.load());
	if(backlog <= 0) return;

	uint32_t frames = (uint32_t) backlog;
	if(frames > config.afeRingFrames){
		// The AFE can't hold this chunk; count it as dropped rather than as backlog.
		afeOverflows++;
		fed -= frames - config.afeRingFrames;
		frames = config.afeRingFrames;
	}
	if(frames > afeBacklogMax){
		afeBacklogMax = frames;
	}
}

uint32_t AfeGlitchMeter::framesToMs(uint32_t frames) const{
	return (uint32_t) ((uint64_t) frames * 1000 / config.sampleRate);
}
