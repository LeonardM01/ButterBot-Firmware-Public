#ifndef BUTTERBOT_FIRMWARE_WIFILINK_H
#define BUTTERBOT_FIRMWARE_WIFILINK_H

#include <cstdint>
#include <string_view>

/**
 * The home link's decisions, apart from the driver: whether it is online, and when to retry after a drop.
 * Pure logic, no ESP-IDF; host-tested in test/host/wifi_logic. WiFiService feeds it the driver's events under
 * its lock and carries out the returned Step, so nothing here blocks or touches the state machine.
 *
 * Home (the default): every drop or failed connect start is retried on the WiFiBackoff schedule, forever; a
 * lease resets the schedule. Controller (RC mode): offline, no retries, the controller network's events ignored.
 */
class WiFiLink {
public:
	// What the caller does after an event.
	struct Step {
		// Arm the reconnect timer for this long; 0 means no retry.
		uint32_t retryInMs = 0;
		// The online state flipped (see online()): log the transition.
		bool onlineChanged = false;
	};

	// Keeps its own copy of `homeSsid`. One longer than an SSID's 32 bytes is not kept, so only unnamed drops match.
	explicit WiFiLink(std::string_view homeSsid) noexcept;

	// WIFI_EVENT_STA_DISCONNECTED for the network named `ssid` (may be empty). In the home profile a drop from
	// another network, such as the controller's after leaving RC, is ignored.
	Step disconnected(std::string_view ssid) noexcept;

	// esp_wifi_connect() returned an error, so no disconnect event will follow.
	Step connectFailed() noexcept;

	// IP_EVENT_STA_GOT_IP: DHCP leased an address.
	Step gotIP() noexcept;

	// IP_EVENT_STA_LOST_IP: the lease expired while associated. DHCP keeps trying, so no retry.
	Step lostIP() noexcept;

	// RC mode borrows the radio: offline, and any pending retry is moot (the caller stops its timer).
	Step useController() noexcept;

	// Back from RC: join home again, on a fresh schedule.
	void useHome() noexcept;

	// Whether the home profile is in use, i.e. a due retry should connect.
	bool homeActive() const noexcept;

	// Online means an address was leased and no disconnect or lost lease followed. Starts offline.
	bool online() const noexcept;

private:
	// NUL-terminated (C2): an SSID is at most 32 bytes.
	char homeSsid[33] = {};
	bool home = true;
	bool isOnline = false;
	uint32_t attempt = 0;

private:
	Step fail() noexcept;
	bool setOnline(bool value) noexcept;
};

#endif //BUTTERBOT_FIRMWARE_WIFILINK_H
