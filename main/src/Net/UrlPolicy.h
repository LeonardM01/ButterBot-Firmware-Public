#ifndef BUTTERBOT_FIRMWARE_URLPOLICY_H
#define BUTTERBOT_FIRMWARE_URLPOLICY_H

#include <string_view>

/**
 * Verdict on the API base URL (#8 points 2-3). Release builds always need https://.
 * Plain http:// is only for laptop development and only when BB_DEV_INSECURE is 1.
 */
enum class UrlPolicy {
	Https,           // allowed
	DevInsecureHttp, // allowed, but audio and the dev token cross the network unencrypted
	Rejected,
};

/**
 * Checks BB_API_URL. constexpr so the firmware can static_assert on secrets.h.
 *
 * The URL is a base the client appends "/v1/ask" to, so it must be scheme + host
 * [+ port] [+ path], without a trailing slash, query, fragment or user:pass@.
 * Whitespace and control characters are rejected (no header or request-line smuggling).
 */
constexpr UrlPolicy checkApiUrl(std::string_view url, bool devInsecure){
	constexpr std::string_view HttpsScheme = "https://";
	constexpr std::string_view HttpScheme = "http://";

	UrlPolicy verdict;
	std::string_view rest;
	if(url.starts_with(HttpsScheme)){
		verdict = UrlPolicy::Https;
		rest = url.substr(HttpsScheme.size());
	}else if(url.starts_with(HttpScheme) && devInsecure){
		verdict = UrlPolicy::DevInsecureHttp;
		rest = url.substr(HttpScheme.size());
	}else{
		return UrlPolicy::Rejected;
	}

	const std::string_view host = rest.substr(0, rest.find('/'));
	if(host.empty() || rest.ends_with('/')){
		return UrlPolicy::Rejected;
	}

	for(const char c : rest){
		const auto u = static_cast<unsigned char>(c);
		if(u <= 0x20 || u == 0x7f || c == '@' || c == '?' || c == '#'){
			return UrlPolicy::Rejected;
		}
	}

	return verdict;
}

#endif //BUTTERBOT_FIRMWARE_URLPOLICY_H
