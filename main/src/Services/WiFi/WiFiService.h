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
#include "WiFiLink.h"

/**
 * Owns the Wi-Fi radio (the CMF WiFi periphery) for the whole runtime.
 *
 * Created once at boot and never destroyed, so the destructor is deleted: destroying the CMF WiFi object deletes
 * the default event loop that esp_netif and esp_http_client also post to.
 *
 * Home profile (the default): joins the network from secrets.h with DHCP and DNS, rejects anything weaker than
 * WPA2-PSK, PMF capable, modem sleep on (required next to BLE), driver config kept in RAM only. A drop is retried
 * forever on the WiFiBackoff schedule, from the sys_evt and esp_timer tasks, so nothing here blocks the state machine,
 * Audio or the LEDs. Online means IP_EVENT_STA_GOT_IP has arrived with no disconnect or lost IP since. The decisions
 * live in WiFiLink (host-tested); this class only carries them out on the driver. Transitions and retries are logged
 * at Warning with the SSID; the password is never logged.
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
	static constexpr EventBits_t OnlineBit = BIT0;

	StrongObjectPtr<WiFi> wifi;
	// Declared in this order: `link` views `home.ssid`, which `homeValid`'s initialiser fills.
	WiFiCredentials::Home home;
	const bool homeValid;

	// Guards `link` and the reconnect timer across the state machine, sys_evt and esp_timer tasks (C7).
	std::mutex lock;
	WiFiLink link;
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
	// Carries out a WiFiLink step: mirrors and logs an online change, arms the reconnect timer.
	void applyLocked(WiFiLink::Step step) noexcept;
};

#endif //BUTTERBOT_FIRMWARE_WIFISERVICE_H
