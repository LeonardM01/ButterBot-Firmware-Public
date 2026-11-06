#include "StreamingPcmSource.h"
#include <cstring>
#include <esp_heap_caps.h>
#include "Util/Elapsed.h"

namespace {
/** Longest single block in write(), so cancel and consumer loss are noticed quickly. */
constexpr TickType_t WriteSlice = pdMS_TO_TICKS(20);
}

std::shared_ptr<PcmStream> PcmStream::create(size_t capacity){
	RingbufHandle_t ring = xRingbufferCreateWithCaps(capacity, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if(ring == nullptr){
		return nullptr;
	}
	return std::shared_ptr<PcmStream>(new PcmStream(ring));
}

PcmStream::PcmStream(RingbufHandle_t ring) : ring(ring){
}

PcmStream::~PcmStream(){
	vRingbufferDeleteWithCaps(ring);
}

bool PcmStream::write(const uint8_t* data, size_t len, TickType_t wait){
	if(len == 0) return true;

	// Count first so the consumer can never see more bytes than bufferedBytes accounts for.
	bufferedBytes += len;
	const TickType_t start = xTaskGetTickCount();
	for(;;){
		if(gone || cancelled) break;

		const TickType_t elapsed = xTaskGetTickCount() - start;
		const TickType_t left = elapsed >= wait ? 0 : wait - elapsed;
		const TickType_t slice = left < WriteSlice ? left : WriteSlice;
		if(xRingbufferSend(ring, data, len, slice) == pdTRUE){
			return true;
		}
		if(left == 0) break;
	}
	bufferedBytes -= len;
	return false;
}

void PcmStream::finish(){
	finished = true;
}

void PcmStream::cancel(){
	cancelled = true;
}

size_t PcmStream::buffered() const{
	return bufferedBytes;
}

bool PcmStream::consumerGone() const{
	return gone;
}

PcmStream::Stats PcmStream::stats() const{
	return {underruns, starvedUs / 1000, bytesPlayed};
}

size_t PcmStream::read(uint8_t* buffer, size_t bytes, TickType_t wait){
	size_t filled = 0;
	// A byte ring hands out at most one contiguous run per receive, so a wrap takes two.
	for(int i = 0; i < 2 && filled < bytes; i++){
		size_t got = 0;
		void* item = xRingbufferReceiveUpTo(ring, &got, i == 0 ? wait : 0, bytes - filled);
		if(item == nullptr) break;
		std::memcpy(buffer + filled, item, got);
		vRingbufferReturnItem(ring, item);
		filled += got;
	}
	bufferedBytes -= filled;
	bytesPlayed += filled;
	return filled;
}

bool PcmStream::ended() const{
	return cancelled || (finished && bufferedBytes == 0);
}

StreamingPcmSource::StreamingPcmSource(std::shared_ptr<PcmStream> stream) : stream(std::move(stream)){
}

StreamingPcmSource::~StreamingPcmSource(){
	// Audio may drop a source without close(), e.g. when playback is disabled.
	close();
}

void StreamingPcmSource::open(){
}

void StreamingPcmSource::close(){
	endStarvation();
	stream->gone = true;
}

size_t StreamingPcmSource::getData(uint8_t* buffer, size_t bytes){
	if(stream->ended()){
		return 0;
	}

	const size_t n = stream->read(buffer, bytes, pdMS_TO_TICKS(ReadWaitMs));
	if(n > 0){
		endStarvation();
		started = true;
		return n;
	}

	if(stream->ended()){
		return 0;
	}

	// The producer is behind. Play silence rather than returning 0, which would stop Audio.
	if(started && !starving){
		starving = true;
		starveStartUs = nowUs();
		stream->underruns++;
	}
	std::memset(buffer, 0, bytes);
	return bytes;
}

void StreamingPcmSource::endStarvation(){
	if(!starving) return;
	stream->starvedUs += (uint32_t) (nowUs() - starveStartUs);
	starving = false;
}

StreamingPcmSource::operator bool() const{
	return !stream->ended();
}
