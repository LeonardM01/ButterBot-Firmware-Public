#ifndef BUTTERBOT_FIRMWARE_WIFICREDENTIALS_H
#define BUTTERBOT_FIRMWARE_WIFICREDENTIALS_H

#include <cstddef>
#include <cstdint>
#include <string_view>

// Length-checked handling of Wi-Fi credentials. Pure logic, no ESP-IDF; host-tested in test/host/wifi_logic.
namespace WiFiCredentials {
	static constexpr size_t MaxSsidLength = 32;

	// constexpr so secrets.h values can also be checked at compile time with static_assert.
	constexpr bool isValidSsid(std::string_view ssid) noexcept{
		return !ssid.empty() && ssid.size() <= MaxSsidLength;
	}

	static constexpr size_t MinPassphraseLength = 8;
	static constexpr size_t MaxPassphraseLength = 63;
	static constexpr size_t RawPskLength = 64;

	// WPA2/WPA3 personal: an 8..63 character passphrase, or a raw PSK given as exactly 64 hex digits.
	constexpr bool isValidPassword(std::string_view password) noexcept{
		if(password.size() >= MinPassphraseLength && password.size() <= MaxPassphraseLength){
			return true;
		}

		if(password.size() != RawPskLength){
			return false;
		}

		for(const char c : password){
			const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
			if(!hex){
				return false;
			}
		}

		return true;
	}

	// Home network credentials as held by the Wi-Fi service: sized for real networks (SSID up to 32 bytes,
	// WPA2/WPA3 passphrase up to 63 characters or a 64-digit hex PSK), always NUL-terminated.
	struct Home {
		char ssid[33] = {};
		char password[65] = {};
	};

	// Fills `out` from the secrets.h values. Returns false and leaves `out` empty when the SSID is empty or longer than
	// 32 bytes, or the password is not an 8..63 character passphrase or a 64-digit hex key.
	bool loadHome(Home& out, const char* ssid, const char* password) noexcept;

	// Copies the string at `src` (reading at most `srcMax` bytes, stopping at a NUL) into `dst` and NUL-terminates it.
	// Returns false and leaves `dst` empty when the string does not fit in `dstSize` bytes including the terminator.
	bool copyString(char* dst, size_t dstSize, const char* src, size_t srcMax) noexcept;

	// Copies the NUL-terminated `src` into a fixed-size driver field such as wifi_config_t::sta.ssid[32].
	// A value that fills the field exactly is stored without a terminator (the driver accepts that); the rest is
	// zero-filled. Returns false and zeroes the whole field when `src` is longer than `fieldSize`.
	bool copyToField(uint8_t* field, size_t fieldSize, const char* src) noexcept;
}

#endif //BUTTERBOT_FIRMWARE_WIFICREDENTIALS_H
