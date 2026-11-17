#include "WiFiLink.h"
#include <cstring>
#include "WiFiBackoff.h"

// RC mode's static station address, 11.0.0.2, in esp_ip4_addr_t::addr order (see WiFi::resetIPInfo).
static constexpr uint32_t ControllerAddress = 11u | (2u << 24);

WiFiLink::WiFiLink(std::string_view ssid) noexcept{
	// Bounded copy (C1); the zero-initialised tail terminates it.
	if(ssid.size() < sizeof(homeSsid)){
		memcpy(homeSsid, ssid.data(), ssid.size());
	}
}

WiFiLink::Step WiFiLink::disconnected(std::string_view ssid) noexcept{
	if(!ssid.empty() && ssid != std::string_view(homeSsid)){
		return {};
	}

	return fail();
}

WiFiLink::Step WiFiLink::connectFailed() noexcept{
	return fail();
}

WiFiLink::Step WiFiLink::gotIP(uint32_t ipv4) noexcept{
	// In RC mode the static controller address raises GOT_IP too; that is not "online", and neither is one of
	// those events still queued when the home profile is back.
	if(!home || ipv4 == ControllerAddress){
		return {};
	}

	lease = ipv4;
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

uint32_t WiFiLink::address() const noexcept{
	return lease;
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
