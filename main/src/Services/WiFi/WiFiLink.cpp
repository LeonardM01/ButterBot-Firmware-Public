#include "WiFiLink.h"
#include "WiFiBackoff.h"

WiFiLink::WiFiLink(std::string_view homeSsid) noexcept : homeSsid(homeSsid){}

WiFiLink::Step WiFiLink::disconnected(std::string_view ssid) noexcept{
	if(!ssid.empty() && ssid != homeSsid){
		return {};
	}

	return fail();
}

WiFiLink::Step WiFiLink::connectFailed() noexcept{
	return fail();
}

WiFiLink::Step WiFiLink::gotIP() noexcept{
	// In RC mode the static controller address raises GOT_IP too; that is not "online".
	if(!home){
		return {};
	}

	attempt = 0;
	return {0, setOnline(true)};
}

WiFiLink::Step WiFiLink::lostIP() noexcept{
	return {0, setOnline(false)};
}

WiFiLink::Step WiFiLink::useController() noexcept{
	home = false;
	attempt = 0;
	return {0, setOnline(false)};
}

void WiFiLink::useHome() noexcept{
	home = true;
	attempt = 0;
}

bool WiFiLink::homeActive() const noexcept{
	return home;
}

bool WiFiLink::online() const noexcept{
	return isOnline;
}

WiFiLink::Step WiFiLink::fail() noexcept{
	if(!home){
		return {};
	}

	const bool changed = setOnline(false);
	const uint32_t delayMs = WiFiBackoff::delayMs(attempt);
	// Saturate: the link retries forever, and WiFiBackoff caps any attempt count.
	if(attempt < UINT32_MAX){
		++attempt;
	}

	return {delayMs, changed};
}

bool WiFiLink::setOnline(bool value) noexcept{
	const bool changed = isOnline != value;
	isOnline = value;
	return changed;
}
