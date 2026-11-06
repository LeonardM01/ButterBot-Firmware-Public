#include "MeasureFormat.h"
#include <cinttypes>
#include <cstdio>

namespace {
const char* outcomeName(AskOutcome outcome){
	switch(outcome){
		case AskOutcome::Play:
			return "play";
		case AskOutcome::DidntUnderstand:
			return "didnt_understand";
		case AskOutcome::ThoughtsLost:
			break;
	}
	return "lost";
}
}

void formatKiBps(char* out, size_t cap, uint32_t bytes, uint32_t ms){
	if(ms == 0){
		snprintf(out, cap, "-");
		return;
	}
	// Tenths of KiB/s, rounded: bytes * 1000 / ms / 1024 * 10.
	const uint64_t tenths = ((uint64_t) bytes * 10000 + (uint64_t) ms * 512) / ((uint64_t) ms * 1024);
	snprintf(out, cap, "%" PRIu64 ".%u", tenths / 10, (unsigned) (tenths % 10));
}

void formatAskMeasure(char* out, size_t cap, const AskResult& r){
	char up[16];
	char down[16];
	formatKiBps(up, sizeof(up), r.uploadBytes, r.uploadMs);
	formatKiBps(down, sizeof(down), r.downloadBytes, r.downloadMs);

	snprintf(out, cap,
			 "BBMEASURE ask outcome=%s status=%d aborted=%d truncated=%d dns_ms=%" PRIu32 " connect_ms=%" PRIu32 " "
			 "up_bytes=%" PRIu32 " up_ms=%" PRIu32 " up_KiB_s=%s first_byte_ms=%" PRIu32 " "
			 "down_bytes=%" PRIu32 " down_ms=%" PRIu32 " down_KiB_s=%s first_audio_ms=%" PRIu32 " "
			 "underruns=%" PRIu32 " starved_ms=%" PRIu32 " played_bytes=%" PRIu32,
			 outcomeName(r.outcome), r.status, r.aborted ? 1 : 0, r.truncated ? 1 : 0, r.dnsMs, r.connectMs,
			 r.uploadBytes, r.uploadMs, up, r.firstByteMs,
			 r.downloadBytes, r.downloadMs, down, r.firstAudioMs,
			 r.underruns, r.starvedMs, r.pcmBytesPlayed);
}

void formatAfeMeasure(char* out, size_t cap, const AfeGlitchMeter::Stats& stats){
	snprintf(out, cap,
			 "BBMEASURE afe mic_overflows=%" PRIu32 " mic_lost_ms=%" PRIu32 " mic_read_errors=%" PRIu32 " "
			 "afe_backlog_max_ms=%" PRIu32 " afe_overflows=%" PRIu32 " fetch_errors=%" PRIu32,
			 stats.micOverflows, stats.micLostMs, stats.micReadErrors,
			 stats.afeBacklogMaxMs, stats.afeOverflows, stats.fetchErrors);
}

void formatStackMeasure(char* out, size_t cap, const char* task, uint32_t sizeBytes, uint32_t minFreeBytes){
	snprintf(out, cap, "BBMEASURE stack task=%s size=%" PRIu32 " min_free=%" PRIu32, task, sizeBytes, minFreeBytes);
}
