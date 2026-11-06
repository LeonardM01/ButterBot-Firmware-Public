#include "AskLines.h"
#include <cstddef>

namespace {

// constexpr tables, so they stay in flash (M9).
constexpr const char* FillerLines[] = {
	"Hmm, let me think",
	"Consulting the butter archives",
	"Let me think about that",
	"Processing, processing",
};

constexpr const char* OfflineLines[] = {
	"My brain is out of Wi-Fi range",
	"I can't reach my brain right now, no Wi-Fi",
	"No Wi-Fi, so my big brain is offline",
};

constexpr const char* LostLines[] = {
	"My thoughts got lost, try again",
	"Oops, my thoughts got lost. Ask me again",
	"Hmm, I lost my train of thought. Try again",
};

template<size_t N>
const char* pickFrom(const char* const (&lines)[N], uint32_t pick){
	return lines[pick % N];
}

}

AskLine lineFor(bool online, const AskResult& result){
	if(!online){
		return AskLine::Offline;
	}
	if(result.aborted){
		return AskLine::None;
	}
	if(result.truncated){
		return AskLine::ThoughtsLost;
	}
	switch(result.outcome){
		case AskOutcome::Play:
			return AskLine::None;
		case AskOutcome::DidntUnderstand:
			return AskLine::DidntUnderstand;
		case AskOutcome::ThoughtsLost:
			break;
	}
	return AskLine::ThoughtsLost;
}

const char* askLineText(AskLine line, uint32_t pick){
	switch(line){
		case AskLine::Offline:
			return pickFrom(OfflineLines, pick);
		case AskLine::ThoughtsLost:
			return pickFrom(LostLines, pick);
		case AskLine::None:
		case AskLine::DidntUnderstand:
			break;
	}
	return nullptr;
}

const char* askFillerText(uint32_t pick){
	return pickFrom(FillerLines, pick);
}
