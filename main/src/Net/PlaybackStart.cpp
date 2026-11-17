#include "PlaybackStart.h"

bool PlaybackStart::ready(size_t received, size_t replyBytes, uint32_t elapsedMs){
	if(replyBytes != 0 && received >= replyBytes) return true; // all of it is here
	if(received < MinBytes) return false;
	if(replyBytes == 0) return true; // no length to plan with: one second of audio
	if(elapsedMs == 0) return false; // no rate measured yet

	// 64-bit throughout (C4): received <= replyBytes < 2^32 and elapsedMs < 2^32, so each product fits.
	const uint64_t restBytes = replyBytes - received;
	const uint64_t restDownloadMs = restBytes * elapsedMs / received;
	const uint64_t neededMs = restDownloadMs * SafetyPercent / 100 + MarginMs;
	const uint64_t playMs = (uint64_t) replyBytes * 1000 / BytesPerSecond;
	return neededMs <= playMs;
}
