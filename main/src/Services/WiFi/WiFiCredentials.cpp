#include "WiFiCredentials.h"
#include <cstring>

bool WiFiCredentials::copyString(char* dst, size_t dstSize, const char* src, size_t srcMax) noexcept{
	if(dst == nullptr || dstSize == 0){
		return false;
	}

	dst[0] = '\0';

	if(src == nullptr){
		return false;
	}

	// Read one byte past what fits so an over-long value is detected instead of silently truncated.
	const size_t len = strnlen(src, srcMax < dstSize ? srcMax : dstSize);
	if(len >= dstSize){
		return false;
	}

	memcpy(dst, src, len);
	dst[len] = '\0';
	return true;
}

bool WiFiCredentials::copyToField(uint8_t* field, size_t fieldSize, const char* src) noexcept{
	memset(field, 0, fieldSize);

	if(src == nullptr){
		return false;
	}

	// Read one byte past the field so an over-long value is detected instead of silently truncated.
	const size_t len = strnlen(src, fieldSize + 1);
	if(len > fieldSize){
		return false;
	}

	memcpy(field, src, len);
	return true;
}

bool WiFiCredentials::loadHome(Home& out, const char* ssid, const char* password) noexcept{
	out = Home{};

	if(ssid == nullptr || password == nullptr){
		return false;
	}

	// Bounded reads: one byte more than either buffer holds is enough to tell "too long".
	const std::string_view ssidView(ssid, strnlen(ssid, sizeof(out.ssid)));
	const std::string_view passwordView(password, strnlen(password, sizeof(out.password)));
	if(!isValidSsid(ssidView) || !isValidPassword(passwordView)){
		return false;
	}

	if(!copyString(out.ssid, sizeof(out.ssid), ssid, sizeof(out.ssid)) ||
	   !copyString(out.password, sizeof(out.password), password, sizeof(out.password))){
		out = Home{};
		return false;
	}

	return true;
}
