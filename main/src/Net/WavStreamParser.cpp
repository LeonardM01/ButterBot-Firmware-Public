#include "WavStreamParser.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr uint16_t FormatPcm = 1;
constexpr uint16_t WantChannels = 1;
constexpr uint32_t WantRate = 16000;
constexpr uint16_t WantBits = 16;
constexpr size_t RiffHeaderBytes = 12;
constexpr size_t ChunkHeaderBytes = 8;
constexpr size_t FmtBytes = 16;

uint16_t le16(const uint8_t* p){
	return p[0] | (p[1] << 8);
}

uint32_t le32(const uint8_t* p){
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

bool isId(const uint8_t* p, const char* id){
	return std::memcmp(p, id, 4) == 0;
}
}

WavStreamParser::Result WavStreamParser::feed(const uint8_t* data, size_t len){
	if(current != State::NeedMore){
		return {current, 0};
	}

	const uint8_t* p = data;
	size_t left = len;

	while(left > 0 && current == State::NeedMore){
		switch(phase){
			case Phase::Riff:
				if(have < 4){
					if(!collect(p, left, 4)) break;
					if(!isId(scratch, "RIFF")) return {fail(), len - left};
				}
				if(!collect(p, left, RiffHeaderBytes)) break;
				if(!isId(scratch + 8, "WAVE")) return {fail(), len - left};
				have = 0;
				phase = Phase::ChunkHeader;
				break;

			case Phase::ChunkHeader:
				if(!collect(p, left, ChunkHeaderBytes)) break;
				have = 0;
				onChunkHeader();
				break;

			case Phase::Fmt:
				if(!collect(p, left, FmtBytes)) break;
				have = 0;
				onFmt();
				break;

			case Phase::Skip: {
				const size_t n = std::min<size_t>(left, skip);
				p += n;
				left -= n;
				consumed += n;
				skip -= n;
				if(skip == 0) phase = Phase::ChunkHeader;
				break;
			}

			case Phase::Done:
				break;
		}
	}

	return {current, len - left};
}

WavStreamParser::State WavStreamParser::state() const{
	return current;
}

std::optional<uint32_t> WavStreamParser::dataBytes() const{
	if(current != State::Pcm || dataSize == 0 || dataSize == 0xFFFFFFFF){
		return std::nullopt;
	}
	return dataSize;
}

WavStreamParser::State WavStreamParser::fail(){
	current = State::Invalid;
	phase = Phase::Done;
	return current;
}

bool WavStreamParser::collect(const uint8_t*& p, size_t& left, size_t want){
	const size_t n = std::min(left, want - have);
	std::memcpy(scratch + have, p, n);
	have += n;
	p += n;
	left -= n;
	consumed += n;
	return have == want;
}

void WavStreamParser::onChunkHeader(){
	const uint32_t size = le32(scratch + 4);

	if(isId(scratch, "data")){
		const bool unknownSize = size == 0 || size == 0xFFFFFFFF;
		if(!sawFmt || (!unknownSize && size % 2 != 0)){
			fail();
			return;
		}
		dataSize = size;
		current = State::Pcm;
		phase = Phase::Done;
		return;
	}

	// Every other chunk's body is header bytes we must get through: refuse oversized ones up front.
	const uint64_t padded = (uint64_t) size + size % 2;
	if(consumed + padded + ChunkHeaderBytes > MaxHeaderBytes){
		fail();
		return;
	}

	if(isId(scratch, "fmt ")){
		if(size < FmtBytes){
			fail();
			return;
		}
		skip = (uint32_t) padded - FmtBytes;
		phase = Phase::Fmt;
		return;
	}

	skip = (uint32_t) padded;
	phase = skip == 0 ? Phase::ChunkHeader : Phase::Skip;
}

void WavStreamParser::onFmt(){
	const uint16_t format = le16(scratch);
	const uint16_t channels = le16(scratch + 2);
	const uint32_t rate = le32(scratch + 4);
	const uint16_t bits = le16(scratch + 14);
	if(format != FormatPcm || channels != WantChannels || rate != WantRate || bits != WantBits){
		fail();
		return;
	}
	sawFmt = true;
	phase = skip == 0 ? Phase::ChunkHeader : Phase::Skip;
}
