#include "SampleAligner.h"

SampleAligner::Aligned SampleAligner::push(const uint8_t* data, size_t len){
	Aligned out = {pair, 0, data, 0};
	if(len == 0) return out;

	if(carry){
		pair[0] = held;
		pair[1] = data[0];
		out.pairLen = 2;
		carry = false;
		data++;
		len--;
	}

	out.body = data;
	out.bodyLen = len & ~(size_t) 1;
	if(len % 2 != 0){
		held = data[len - 1];
		carry = true;
	}
	return out;
}

bool SampleAligner::hasCarry() const{
	return carry;
}
