set(WIFI_LOGIC_DIR "../../../../main/src/Services/WiFi")

idf_component_register(SRCS "test_main.cpp"
                            "test_backoff.cpp"
                            "${WIFI_LOGIC_DIR}/WiFiBackoff.cpp"
                       INCLUDE_DIRS "${WIFI_LOGIC_DIR}"
                       REQUIRES unity)
