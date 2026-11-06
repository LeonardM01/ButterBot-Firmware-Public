#ifndef BUTTERBOT_FIRMWARE_WIFISERVICE_H
#define BUTTERBOT_FIRMWARE_WIFISERVICE_H

#include <cstdint>
#include <mutex>
#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <Periphery/WiFi.h>
#include "WiFiCredentials.h"

/**
 * Owns the Wi-Fi radio (the CMF WiFi periphery) for the whole runtime.
 *
 * Created once at boot and never destroyed, so the destructor is deleted: destroying the CMF WiFi object deletes
 * the default event loop that esp_netif and esp_http_client also post to.
 *
 * Home profile (the default): joins the network from secrets.h with DHCP and DNS, rejects anything weaker than
 * WPA2-PSK, PMF capable, modem sleep on (required next to BLE), driver config kept in RAM only. A drop is retried
 * forever on the WiFiBackoff schedule. Online means IP_EVENT_STA_GOT_IP has arrived with no disconnect or lost IP
 * since. Transitions are logged at Warning; credentials are never logged.
 *
 * Controller profile (RC mode): RCState borrows the radio, which runs on the controller network at the static
 * 11.0.0.2 with the existing WiFiStation logic and no reconnect from this service. Always offline.
 */
class WiFiService {
public:
	WiFiService() noexcept;
	~WiFiService() = delete;
	WiFiService(const WiFiService&) = delete;
	WiFiService& operator=(const WiFiService&) = delete;

	bool isOnline() const noexcept;

	// Blocks the calling task until online or until `timeoutMs` has passed. Returns whether it is online.
	bool waitOnline(uint32_t timeoutMs) const noexcept;

	// Leaves the home network, stops DHCP and sets the static RC address. Returns the periphery for WiFiStation to
	// join the controller network with.
	WiFi* useControllerProfile() noexcept;

	// Leaves the controller network and rejoins home with DHCP and the reconnect schedule.
	void useHomeProfile() noexcept;

private:
	enum class Profile : uint8_t {
		Home,
		Controller
	};

	static constexpr EventBits_t OnlineBit = BIT0;

	StrongObjectPtr<WiFi> wifi;
	WiFiCredentials::Home home;
	bool homeValid = false;

	// Guards profile, attempt and the reconnect timer across the state machine, sys_evt and esp_timer tasks.
	std::mutex lock;
	Profile profile = Profile::Home;
	uint32_t attempt = 0;
	esp_timer_handle_t reconnectTimer = nullptr;

	StaticEventGroup_t eventsBuffer;
	EventGroupHandle_t events = nullptr;

	esp_event_handler_instance_t wifiHandler = nullptr;
	esp_event_handler_instance_t ipHandler = nullptr;

private:
	static void onEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
	static void onReconnectTimer(void* arg);

	void onDisconnected(const wifi_event_sta_disconnected_t& event) noexcept;
	void onGotIP() noexcept;
	void onLostIP() noexcept;

	void applyHomeConfig() noexcept;
	void connectHomeLocked() noexcept;
	// Arms the reconnect timer for the next backoff step and returns its delay.
	uint32_t scheduleReconnectLocked() noexcept;
	void setOnlineLocked() noexcept;
	void setOfflineLocked() noexcept;
};

#endif //BUTTERBOT_FIRMWARE_WIFISERVICE_H
