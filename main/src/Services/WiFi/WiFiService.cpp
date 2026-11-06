#include "WiFiService.h"
#include <string_view>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <Log/Log.h>
#include <Memory/ObjectMemory.h>
#include "WiFiBackoff.h"
// The only translation unit that includes the home network credentials (main/secrets.h, git-ignored).
#include "../../../secrets.h"

DEFINE_LOG(WiFiService)

static_assert(WiFiCredentials::isValidSsid(BB_WIFI_SSID), "BB_WIFI_SSID in main/secrets.h must be 1 to 32 bytes.");
static_assert(WiFiCredentials::isValidPassword(BB_WIFI_PASSWORD),
			  "BB_WIFI_PASSWORD in main/secrets.h must be an 8 to 63 character passphrase or 64 hex digits.");

WiFiService::WiFiService() noexcept{
	events = xEventGroupCreateStatic(&eventsBuffer);

	homeValid = WiFiCredentials::loadHome(home, BB_WIFI_SSID, BB_WIFI_PASSWORD);
	if(!homeValid){
		CMF_LOG(WiFiService, Error, "Home Wi-Fi credentials are invalid, staying offline.");
	}

	const esp_timer_create_args_t timerArgs = {
		.callback = &WiFiService::onReconnectTimer,
		.arg = this,
		.dispatch_method = ESP_TIMER_TASK,
		.name = "WiFiRetry",
		.skip_unhandled_events = true,
	};
	ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &reconnectTimer));

	// Creates the default event loop, so it has to exist before the handlers below are registered.
	wifi = newObject<WiFi>();

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &WiFiService::onEvent, this, &wifiHandler));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &WiFiService::onEvent, this, &ipHandler));

	wifi->startStation(/*dhcpClient=*/true, WIFI_STORAGE_RAM);

	// Modem sleep is required for Wi-Fi and BLE coexistence.
	esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

	applyHomeConfig();

	std::lock_guard guard(lock);
	connectHomeLocked();
}

bool WiFiService::isOnline() const noexcept{
	return (xEventGroupGetBits(events) & OnlineBit) != 0;
}

bool WiFiService::waitOnline(uint32_t timeoutMs) const noexcept{
	const EventBits_t bits = xEventGroupWaitBits(events, OnlineBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
	return (bits & OnlineBit) != 0;
}

WiFi* WiFiService::useControllerProfile() noexcept{
	{
		std::lock_guard guard(lock);
		profile = Profile::Controller;
		attempt = 0;
		esp_timer_stop(reconnectTimer);
		setOfflineLocked();
	}

	CMF_LOG(WiFiService, LogLevel::Warning, "Leaving home Wi-Fi for RC mode.");

	esp_wifi_disconnect();

	// A static address cannot be set while the DHCP client runs.
	esp_netif_dhcpc_stop(wifi->getNetif());
	wifi->resetIPInfo();

	return *wifi;
}

void WiFiService::useHomeProfile() noexcept{
	esp_wifi_scan_stop();
	esp_wifi_disconnect();

	{
		std::lock_guard guard(lock);
		profile = Profile::Home;
		attempt = 0;
	}

	CMF_LOG(WiFiService, LogLevel::Warning, "Rejoining home Wi-Fi.");

	// Clears the static RC address; if the link is down DHCP starts on the next association.
	esp_netif_dhcpc_start(wifi->getNetif());

	applyHomeConfig();

	std::lock_guard guard(lock);
	connectHomeLocked();
}

void WiFiService::onEvent(void* arg, esp_event_base_t base, int32_t id, void* data){
	// Runs in the sys_evt task (small stack): only flags, a timer and short logs here.
	auto* self = static_cast<WiFiService*>(arg);
	if(self == nullptr){
		return;
	}

	if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && data != nullptr){
		self->onDisconnected(*static_cast<const wifi_event_sta_disconnected_t*>(data));
	}else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP){
		self->onGotIP();
	}else if(base == IP_EVENT && id == IP_EVENT_STA_LOST_IP){
		self->onLostIP();
	}
}

void WiFiService::onReconnectTimer(void* arg){
	auto* self = static_cast<WiFiService*>(arg);
	if(self == nullptr){
		return;
	}

	std::lock_guard guard(self->lock);
	self->connectHomeLocked();
}

void WiFiService::onDisconnected(const wifi_event_sta_disconnected_t& event) noexcept{
	std::lock_guard guard(lock);

	if(profile != Profile::Home){
		return;
	}

	// The disconnect from the controller network arrives after switching back to home; it is not a home failure.
	const size_t ssidLen = event.ssid_len < sizeof(event.ssid) ? event.ssid_len : sizeof(event.ssid);
	const std::string_view eventSsid(reinterpret_cast<const char*>(event.ssid), ssidLen);
	if(!eventSsid.empty() && eventSsid != std::string_view(home.ssid)){
		return;
	}

	setOfflineLocked();
	const uint32_t delayMs = scheduleReconnectLocked();
	CMF_LOG(WiFiService, LogLevel::Warning, "Home Wi-Fi disconnected (reason %u), retry in %lu ms.",
			static_cast<unsigned>(event.reason), static_cast<unsigned long>(delayMs));
}

void WiFiService::onGotIP() noexcept{
	std::lock_guard guard(lock);

	// In RC mode the static address also raises GOT_IP; that is not "online".
	if(profile != Profile::Home){
		return;
	}

	attempt = 0;
	setOnlineLocked();
}

void WiFiService::onLostIP() noexcept{
	std::lock_guard guard(lock);

	if(profile != Profile::Home){
		return;
	}

	setOfflineLocked();
}

void WiFiService::applyHomeConfig() noexcept{
	if(!homeValid){
		return;
	}

	wifi_config_t cfg = {};
	WiFiCredentials::copyToField(cfg.sta.ssid, sizeof(cfg.sta.ssid), home.ssid);
	WiFiCredentials::copyToField(cfg.sta.password, sizeof(cfg.sta.password), home.password);

	// Security floor: refuse open, WEP and WPA networks; WPA3-SAE (and transition mode) allowed; PMF when offered.
	cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	cfg.sta.pmf_cfg.capable = true;
	cfg.sta.pmf_cfg.required = false;
	cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
	if(err != ESP_OK){
		CMF_LOG(WiFiService, Error, "Applying the home Wi-Fi config failed: %s", esp_err_to_name(err));
	}
}

void WiFiService::connectHomeLocked() noexcept{
	if(profile != Profile::Home || !homeValid){
		return;
	}

	// No scan of our own: esp_wifi_connect scans every channel for the configured network.
	const esp_err_t err = esp_wifi_connect();
	if(err != ESP_OK){
		const uint32_t delayMs = scheduleReconnectLocked();
		CMF_LOG(WiFiService, LogLevel::Warning, "Home Wi-Fi connect failed to start (%s), retry in %lu ms.",
				esp_err_to_name(err), static_cast<unsigned long>(delayMs));
	}
}

uint32_t WiFiService::scheduleReconnectLocked() noexcept{
	const uint32_t delayMs = WiFiBackoff::delayMs(attempt);
	if(attempt < UINT32_MAX){
		++attempt;
	}

	esp_timer_stop(reconnectTimer);
	esp_timer_start_once(reconnectTimer, static_cast<uint64_t>(delayMs) * 1000);

	return delayMs;
}

// Callers hold `lock`, so reading then changing the bit cannot interleave with another transition.
void WiFiService::setOnlineLocked() noexcept{
	if(!isOnline()){
		xEventGroupSetBits(events, OnlineBit);
		CMF_LOG(WiFiService, LogLevel::Warning, "Wi-Fi online.");
	}
}

void WiFiService::setOfflineLocked() noexcept{
	if(isOnline()){
		xEventGroupClearBits(events, OnlineBit);
		CMF_LOG(WiFiService, LogLevel::Warning, "Wi-Fi offline.");
	}
}
