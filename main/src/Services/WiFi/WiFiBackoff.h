#ifndef BUTTERBOT_FIRMWARE_WIFIBACKOFF_H
#define BUTTERBOT_FIRMWARE_WIFIBACKOFF_H

#include <cstdint>

// Reconnect schedule for the home Wi-Fi profile: 1 s, 2 s, 4 s ... capped at 60 s, forever.
// Pure logic, no ESP-IDF; host-tested in test/host/wifi_logic.
namespace WiFiBackoff {
	static constexpr uint32_t InitialDelayMs = 1000;
	static constexpr uint32_t MaxDelayMs = 60000;

	// Delay before reconnect attempt number `attempt` (0 = first retry after a drop).
	uint32_t delayMs(uint32_t attempt) noexcept;
}

#endif //BUTTERBOT_FIRMWARE_WIFIBACKOFF_H
