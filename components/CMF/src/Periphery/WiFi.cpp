#include "WiFi.h"
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_types_generic.h>
#include "Log/Log.h"

DEFINE_LOG(WiFi)

WiFi::WiFi() noexcept : Super() {
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, [](void* arg, esp_event_base_t base, int32_t id, void* data) {
        if(base != WIFI_EVENT){
            return;
        }

        auto wifi = static_cast<WiFi*>(arg);
        if(wifi == nullptr){
            return;
        }

        wifi->onNativeEvent(id, data);
    }, this, &nativeEventHandler);
}

WiFi::~WiFi(){
	if(type == WiFiType::Station){
		esp_wifi_disconnect();
	}

	if(type != WiFiType::None){
		esp_wifi_stop();
		esp_wifi_deinit();
	}

	esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, nativeEventHandler);

	if(netif != nullptr){
		esp_wifi_clear_default_wifi_driver_and_handlers(netif);
		esp_netif_destroy(netif);
		netif = nullptr;
	}

	esp_netif_deinit();
	esp_event_loop_delete_default();

	type = WiFiType::None;
}

#ifdef CONFIG_CMF_WIFI_AP_SUPPORT

void WiFi::startAccessPoint(const std::string& name, const std::string& password, uint8_t channel /*= 1*/, uint8_t maxConnections /*= 1*/) noexcept {
    if(type != WiFiType::None) {
        CMF_LOG(WiFi, LogLevel::Warning, "WiFi access point attempted to start when WiFi is already running.");
        return;
    }

    type = WiFiType::AccessPoint;

    createNetif();

    const wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg_wifi);

    wifi_config_t cfg_ap = {
        .ap = {
            .channel = channel,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = false,
            .max_connection = maxConnections,
        },
    };

    if(!copyCredentials(cfg_ap.ap.ssid, sizeof(cfg_ap.ap.ssid), cfg_ap.ap.password, sizeof(cfg_ap.ap.password), name, password)) {
        CMF_LOG(WiFi, LogLevel::Error, "WiFi access point SSID or password too long, not started.");
        return;
    }
    cfg_ap.ap.ssid_len = name.size();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg_ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    initSemaphore.acquire();
}

bool WiFi::isHidden() const noexcept {
    if(type == WiFiType::AccessPoint) {
        return false;
    }

    wifi_config_t config;
    esp_wifi_get_config(WIFI_IF_AP, &config);

    return config.ap.ssid_hidden;
}

void WiFi::setHidden(bool hidden) const noexcept {
    if(type == WiFiType::AccessPoint) {
        return;
    }

    wifi_config_t config;
    esp_wifi_get_config(WIFI_IF_AP, &config);
    config.ap.ssid_hidden = hidden;
    esp_wifi_set_config(WIFI_IF_AP, &config);
}

void WiFi::setNetworkParameters(const std::string& name, const std::string& password) const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    wifi_config_t config = {};
    esp_wifi_get_config(WIFI_IF_AP, &config);

    if(!copyCredentials(config.ap.ssid, sizeof(config.ap.ssid), config.ap.password, sizeof(config.ap.password), name, password)) {
        CMF_LOG(WiFi, LogLevel::Error, "WiFi access point SSID or password too long, not applied.");
        return;
    }
    config.ap.ssid_len = name.size();

    esp_wifi_set_config(WIFI_IF_AP, &config);
}

#endif

#ifdef CONFIG_CMF_WIFI_STA_SUPPORT

void WiFi::startStation(StationAddressing addressing /*= Static11*/, wifi_storage_t storage /*= WIFI_STORAGE_FLASH*/) noexcept {
	if(type != WiFiType::None) {
		CMF_LOG(WiFi, LogLevel::Warning, "WiFi station attempted to start when WiFi is already running.");
		return;
	}

	type = WiFiType::Station;

	createNetif(addressing);

	const wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg_wifi);
	// Must precede any esp_wifi_set_config so the chosen storage applies to every configuration.
	ESP_ERROR_CHECK(esp_wifi_set_storage(storage));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	initSemaphore.acquire();
}

esp_netif_t* WiFi::getNetif() const noexcept {
    return netif;
}

void WiFi::resetIPInfo() const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    const esp_netif_ip_info_t ip = {
        .ip =		{ .addr = esp_ip4addr_aton("11.0.0.2") },
        .netmask =	{ .addr = esp_ip4addr_aton("255.255.255.0") },
        .gw =		{ .addr = esp_ip4addr_aton("11.0.0.1") },
    };

    const auto netif = esp_netif_get_default_netif();
    esp_netif_set_ip_info(netif, &ip);
}

void WiFi::connect() const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    esp_wifi_connect();
}

void WiFi::startScanning(wifi_scan_type_t scanType /*= WIFI_SCAN_TYPE_PASSIVE*/, wifi_scan_time_t scanTime /*= {.active = {.min = 0, .max = 1500}, .passive = 1500}*/) const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    const wifi_scan_config_t ScanConfig = {
        .channel = 1,
        .scan_type = scanType,
        .scan_time = scanTime
    };

    esp_wifi_scan_start(&ScanConfig, false);
}

void WiFi::stopScanning() const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    esp_wifi_scan_stop();
}

void WiFi::disconnect() const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    esp_wifi_disconnect();
}

int WiFi::getConnectionRSSI() const noexcept {
    if(type != WiFiType::Station) {
        return -1;
    }

    int rssi = -1;
    if(esp_wifi_sta_get_rssi(&rssi) != ESP_OK){
        return -1;
    }

    return rssi;
}

void WiFi::setTargetParameters(const std::string& ssid, const std::string& password) const noexcept {
    if(type != WiFiType::Station) {
        return;
    }

    wifi_config_t cfg_wifi = {};

    if(!copyCredentials(cfg_wifi.sta.ssid, sizeof(cfg_wifi.sta.ssid), cfg_wifi.sta.password, sizeof(cfg_wifi.sta.password), ssid, password)) {
        CMF_LOG(WiFi, LogLevel::Error, "WiFi station SSID or password too long, not applied.");
        return;
    }

    // The values come from the controller over BLE: a config the driver refuses must not reboot the robot (E2).
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg_wifi);
    if(err != ESP_OK) {
        CMF_LOG(WiFi, LogLevel::Error, "WiFi station config refused: %s", esp_err_to_name(err));
    }
}

std::vector<wifi_ap_record_t> WiFi::getAPRecords(size_t maxScanSize) const noexcept {
    std::vector<wifi_ap_record_t> result(maxScanSize);

    memset(result.data(), 0, maxScanSize * sizeof(wifi_ap_record_t));

    uint16_t number = maxScanSize;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, result.data()));
    result.resize(number);

    return result;
}
#endif

bool WiFi::copyCredentials(uint8_t* ssidField, size_t ssidSize, uint8_t* passwordField, size_t passwordSize,
                           const std::string& ssid, const std::string& password) noexcept {
    // Bounded copies (C1): a value that fills its field exactly is stored without a terminator (the driver accepts
    // that); a longer one is rejected rather than overflowing into the next field.
    if(ssid.size() > ssidSize || password.size() > passwordSize) {
        return false;
    }

    // Callers may pass a config read back from the driver: clear the previous, possibly longer, values first.
    memset(ssidField, 0, ssidSize);
    memset(passwordField, 0, passwordSize);
    memcpy(ssidField, ssid.data(), ssid.size());
    memcpy(passwordField, password.data(), password.size());
    return true;
}

void WiFi::onNativeEvent(uint32_t id, void *data) noexcept {
    CMF_LOG(WiFi, LogLevel::Info, "WiFi native event received: %ld", id);

    if(id == WIFI_EVENT_STA_START || id == WIFI_EVENT_AP_START) {
        initSemaphore.release();
    }

    switch(id) {
        case WIFI_EVENT_WIFI_READY: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_SCAN_DONE: {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
            const wifi_event_sta_scan_done_t* scan_done = static_cast<wifi_event_sta_scan_done_t*>(data);
            if(scan_done == nullptr) {
                break;
            }

            OnScanDone.broadcast(scan_done->status, scan_done->number, scan_done->scan_id);
#endif
            break;
        }
        case WIFI_EVENT_STA_START: {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
            OnStationStart.broadcast();
#endif
            break;
        }
        case WIFI_EVENT_STA_STOP: {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
            OnStationStop.broadcast();
#endif
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
            const wifi_event_sta_connected_t* connected = static_cast<wifi_event_sta_connected_t*>(data);
            if(connected == nullptr) {
                break;
            }

            //const std::string ssid(reinterpret_cast<const char*>(connected->ssid), connected->ssid_len);
            //const std::string bssid(reinterpret_cast<const char*>(connected->bssid), 6);

            OnStationConnected.broadcast(/*ssid, bssid,*/ connected->channel, connected->authmode, connected->aid);
#endif
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
            const wifi_event_sta_disconnected_t* disconnected = static_cast<wifi_event_sta_disconnected_t*>(data);
            if(disconnected == nullptr) {
                break;
            }

            //const std::string ssid(reinterpret_cast<const char*>(disconnected->ssid), disconnected->ssid_len);
            //const std::string bssid(reinterpret_cast<const char*>(disconnected->bssid), 6);

            OnStationDisconnected.broadcast(/*ssid, bssid,*/ disconnected->reason, disconnected->rssi);
#endif
            break;
        }
        case WIFI_EVENT_STA_AUTHMODE_CHANGE: {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
            const wifi_event_sta_authmode_change_t* auth = static_cast<wifi_event_sta_authmode_change_t*>(data);
            if(auth == nullptr) {
                break;
            }

            OnStationAuthModeChanged.broadcast(auth->old_mode, auth->new_mode);
#endif
            break;
        }
        case WIFI_EVENT_STA_WPS_ER_SUCCESS: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_STA_WPS_ER_PIN: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_AP_START: {
#ifdef CONFIG_CMF_WIFI_AP_SUPPORT
            OnAccessPointStart.broadcast();
#endif
            break;
        }
        case WIFI_EVENT_AP_STOP: {
#ifdef CONFIG_CMF_WIFI_AP_SUPPORT
        	OnAccessPointStop.broadcast();
#endif
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
#ifdef CONFIG_CMF_WIFI_AP_SUPPORT
        	const wifi_event_ap_staconnected_t* connected = static_cast<wifi_event_ap_staconnected_t*>(data);
            if(connected == nullptr) {
                break;
            }

            //const std::string mac(reinterpret_cast<const char *>(connected->mac), 6);

            OnAccessPointConnection.broadcast(/*mac,*/ connected->aid, connected->is_mesh_child);
#endif
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
#ifdef CONFIG_CMF_WIFI_AP_SUPPORT
            const wifi_event_ap_stadisconnected_t* disconnected = static_cast<wifi_event_ap_stadisconnected_t*>(data);
            if(disconnected == nullptr) {
                break;
            }

            //const std::string mac(reinterpret_cast<const char *>(disconnected->mac), 6);

            OnAccessPointDisconnection.broadcast(/*mac,*/ disconnected->aid, disconnected->is_mesh_child, disconnected->reason);
#endif
            break;
        }
        case WIFI_EVENT_AP_PROBEREQRECVED: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_STA_BEACON_TIMEOUT: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_AP_WPS_RG_SUCCESS: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_AP_WPS_RG_FAILED: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_AP_WPS_RG_TIMEOUT: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_AP_WPS_RG_PIN: {
            // Not yet supported.
            break;
        }
        case WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP: {
            // Not yet supported.
            break;
        }
        default: {
            break;
        }
    }
}

void WiFi::createNetif(StationAddressing addressing /*= Static11*/) noexcept {
    if(type != WiFiType::AccessPoint && type != WiFiType::Station) {
        CMF_LOG(WiFi, LogLevel::Warning, "Attempted to create a netif for a WiFi type that is not yet supported.");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());

	// ReSharper disable once CppDFAConstantConditions
    if(type == WiFiType::AccessPoint) {
#ifdef CONFIG_CMF_WIFI_AP_SUPPORT
        esp_netif_inherent_config_t base{};
        memcpy(&base, ESP_NETIF_BASE_DEFAULT_WIFI_AP, sizeof(esp_netif_inherent_config_t));
        base.flags = (esp_netif_flags_t) ((base.flags & ~(ESP_NETIF_DHCP_SERVER | ESP_NETIF_DHCP_CLIENT | ESP_NETIF_FLAG_EVENT_IP_MODIFIED)) | ESP_NETIF_FLAG_GARP);

        const esp_netif_ip_info_t ip = {
            .ip =		{ .addr = esp_ip4addr_aton("11.0.0.1") },
            .netmask =	{ .addr = esp_ip4addr_aton("255.255.255.0") },
            .gw =		{ .addr = esp_ip4addr_aton("11.0.0.1") },
        };
        base.ip_info = &ip;

        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_AP();
        cfg.base = &base;

        esp_netif_t* netif = esp_netif_new(&cfg);
        if(netif == nullptr){
            CMF_LOG(WiFi, LogLevel::Error, "esp_netif_new returned nullptr for WiFi access point");
        }
        assert(netif);
        esp_netif_set_default_netif(netif);

        esp_netif_attach_wifi_ap(netif);
        esp_wifi_set_default_wifi_ap_handlers();
#endif
	// ReSharper disable once CppDFAConstantConditions
    }else if(type == WiFiType::Station && addressing == StationAddressing::Dhcp) {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
        // Stock station netif: DHCP client on, IP_EVENT_STA_GOT_IP posted once an address is leased.
        const esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_STA();

        netif = esp_netif_new(&cfg);
        if(netif == nullptr){
            CMF_LOG(WiFi, LogLevel::Error, "esp_netif_new returned nullptr for WiFi station");
        }
        assert(netif);
        esp_netif_set_default_netif(netif);

        esp_netif_attach_wifi_station(netif);
        esp_wifi_set_default_wifi_sta_handlers();
#endif
	// ReSharper disable once CppDFAConstantConditions
    }else if(type == WiFiType::Station) {
#ifdef CONFIG_CMF_WIFI_STA_SUPPORT
        esp_netif_inherent_config_t base{};
        memcpy(&base, ESP_NETIF_BASE_DEFAULT_WIFI_STA, sizeof(esp_netif_inherent_config_t));
        base.flags = (esp_netif_flags_t) ((base.flags & ~(ESP_NETIF_DHCP_SERVER | ESP_NETIF_DHCP_CLIENT | ESP_NETIF_FLAG_EVENT_IP_MODIFIED)) | ESP_NETIF_FLAG_GARP);

        const esp_netif_ip_info_t ip = {
            .ip =		{ .addr = esp_ip4addr_aton("11.0.0.2") },
            .netmask =	{ .addr = esp_ip4addr_aton("255.255.255.0") },
            .gw =		{ .addr = esp_ip4addr_aton("11.0.0.1") },
        };
        base.ip_info = &ip;

        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_STA();
        cfg.base = &base;

    	netif = esp_netif_new(&cfg);
        if(netif == nullptr){
            CMF_LOG(WiFi, LogLevel::Error, "esp_netif_new returned nullptr for WiFi station");
        }
        assert(netif);
        esp_netif_set_default_netif(netif);

        esp_netif_attach_wifi_station(netif);
        esp_wifi_set_default_wifi_sta_handlers();
#endif
    }
}
