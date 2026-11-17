set(WIFI_LOGIC_DIR "../../../../main/src/Services/WiFi")

idf_component_register(SRCS "test_main.cpp"
                            "test_backoff.cpp"
                            "test_credentials.cpp"
                            "test_link.cpp"
                            "${WIFI_LOGIC_DIR}/WiFiBackoff.cpp"
                            "${WIFI_LOGIC_DIR}/WiFiCredentials.cpp"
                            "${WIFI_LOGIC_DIR}/WiFiLink.cpp"
                       INCLUDE_DIRS "${WIFI_LOGIC_DIR}"
                       REQUIRES unity)
