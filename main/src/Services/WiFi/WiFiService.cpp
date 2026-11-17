#include "WiFiService.h"
#include <string_view>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <Log/Log.h>
#include <Memory/ObjectMemory.h>
// The only translation unit that includes the home network credentials (main/secrets.h, git-ignored).
#include "../../../secrets.h"

DEFINE_LOG(WiFiService)

static_assert(WiFiCredentials::isValidSsid(BB_WIFI_SSID), "BB_WIFI_SSID in main/secrets.h must be 1 to 32 bytes.");
static_assert(WiFiCredentials::isValidPassword(BB_WIFI_PASSWORD),
			  "BB_WIFI_PASSWORD in main/secrets.h must be an 8 to 63 character passphrase or 64 hex digits.");

// Logs a failed driver call and carries on (E3, E4): every caller below has a retry or a later event to fall back on.
// `benign` is an error code that only means "already in that state".
static void check(esp_err_t err, const char* what, esp_err_t benign = ESP_OK){
	if(err != ESP_OK && err != benign){
		CMF_LOG(WiFiService, Error, "%s failed: %s", what, esp_err_to_name(err));
	}
}

WiFiService::WiFiService() noexcept : homeValid(WiFiCredentials::loadHome(home, BB_WIFI_SSID, BB_WIFI_PASSWORD)),
									  link(BB_WIFI_SSID){
	events = xEventGroupCreateStatic(&eventsBuffer);

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

	wifi->startStation(WiFi::StationAddressing::Dhcp, WIFI_STORAGE_RAM);

	// Modem sleep is required for Wi-Fi and BLE coexistence.
	check(esp_wifi_set_ps(WIFI_PS_MIN_MODEM), "Modem sleep");

	applyHomeConfig();

	if(homeValid){
		CMF_LOG(WiFiService, LogLevel::Warning, "Joining home Wi-Fi \"%s\".", home.ssid);
	}

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
		applyLocked(link.useController());
		check(esp_timer_stop(reconnectTimer), "Stopping the Wi-Fi retry", ESP_ERR_INVALID_STATE);
	}

	CMF_LOG(WiFiService, LogLevel::Warning, "Leaving home Wi-Fi for RC mode.");

	check(esp_wifi_disconnect(), "Leaving home Wi-Fi");

	// A static address cannot be set while the DHCP client runs.
	check(esp_netif_dhcpc_stop(wifi->getNetif()), "Stopping DHCP", ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED);
	wifi->resetIPInfo();

	return *wifi;
}

void WiFiService::useHomeProfile() noexcept{
	check(esp_wifi_scan_stop(), "Stopping the controller scan");
	check(esp_wifi_disconnect(), "Leaving the controller network");

	{
		std::lock_guard guard(lock);
		link.useHome();
	}

	CMF_LOG(WiFiService, LogLevel::Warning, "Rejoining home Wi-Fi \"%s\".", home.ssid);

	// Clears the static RC address; if the link is down DHCP starts on the next association.
	check(esp_netif_dhcpc_start(wifi->getNetif()), "Starting DHCP", ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED);

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
		self->onGotIP(static_cast<const ip_event_got_ip_t*>(data));
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
	// ssid is 32 bytes with no terminator when full; ssid_len is the driver's, so clamp it anyway (C3).
	const size_t ssidLen = event.ssid_len < sizeof(event.ssid) ? event.ssid_len : sizeof(event.ssid);
	const std::string_view ssid(reinterpret_cast<const char*>(event.ssid), ssidLen);

	std::lock_guard guard(lock);
	const WiFiLink::Step step = link.disconnected(ssid);
	applyLocked(step);

	if(step.retryInMs != 0){
		CMF_LOG(WiFiService, LogLevel::Warning, "Home Wi-Fi disconnected (reason %u), retry in %lu ms.",
				static_cast<unsigned>(event.reason), static_cast<unsigned long>(step.retryInMs));
	}
}

void WiFiService::onGotIP(const ip_event_got_ip_t* event) noexcept{
	std::lock_guard guard(lock);
	applyLocked(link.gotIP(), event != nullptr ? &event->ip_info : nullptr);
}

void WiFiService::onLostIP() noexcept{
	std::lock_guard guard(lock);
	applyLocked(link.lostIP());
}

void WiFiService::applyHomeConfig() noexcept{
	if(!homeValid){
		return;
	}

	wifi_config_t cfg = {};
	// loadHome validated both lengths, so these copies fit (C1).
	if(!WiFiCredentials::copyToField(cfg.sta.ssid, sizeof(cfg.sta.ssid), home.ssid) ||
	   !WiFiCredentials::copyToField(cfg.sta.password, sizeof(cfg.sta.password), home.password)){
		CMF_LOG(WiFiService, Error, "Home Wi-Fi credentials do not fit the driver config.");
		WiFiCredentials::wipe(&cfg, sizeof(cfg));
		return;
	}

	// Security floor: refuse open, WEP and WPA networks; WPA3-SAE (and transition mode) allowed; PMF when offered.
	cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	cfg.sta.pmf_cfg.capable = true;
	cfg.sta.pmf_cfg.required = false;
	cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	check(esp_wifi_set_config(WIFI_IF_STA, &cfg), "Applying the home Wi-Fi config");

	// The driver keeps its own copy (in RAM, WIFI_STORAGE_RAM); this stack copy of the password goes now (C8).
	WiFiCredentials::wipe(&cfg, sizeof(cfg));
}

void WiFiService::connectHomeLocked() noexcept{
	if(!link.homeActive() || !homeValid){
		return;
	}

	// No scan of our own: esp_wifi_connect scans every channel for the configured network.
	const esp_err_t err = esp_wifi_connect();
	if(err != ESP_OK){
		const WiFiLink::Step step = link.connectFailed();
		applyLocked(step);
		CMF_LOG(WiFiService, LogLevel::Warning, "Home Wi-Fi connect failed to start (%s), retry in %lu ms.",
				esp_err_to_name(err), static_cast<unsigned long>(step.retryInMs));
	}
}

void WiFiService::applyLocked(WiFiLink::Step step, const esp_netif_ip_info_t* ip) noexcept{
	if(step.onlineChanged){
		if(link.online()){
			xEventGroupSetBits(events, OnlineBit);
			// The leased (DHCP) address, so a tester can tell it from RC mode's static 11.0.0.2. No credentials.
			const esp_ip4_addr_t addr = ip != nullptr ? ip->ip : esp_ip4_addr_t{};
			CMF_LOG(WiFiService, LogLevel::Warning, "Home Wi-Fi online: \"%s\", IP " IPSTR ".", home.ssid, IP2STR(&addr));
		}else{
			xEventGroupClearBits(events, OnlineBit);
			CMF_LOG(WiFiService, LogLevel::Warning, "Home Wi-Fi offline: \"%s\".", home.ssid);
		}
	}

	if(step.retryInMs != 0){
		check(esp_timer_stop(reconnectTimer), "Stopping the Wi-Fi retry", ESP_ERR_INVALID_STATE);
		check(esp_timer_start_once(reconnectTimer, static_cast<uint64_t>(step.retryInMs) * 1000), "Arming the Wi-Fi retry");
	}
}
