# Wi-Fi logic host tests

Unit tests for the ESP-IDF-free parts of the Wi-Fi service, run on the laptop (no robot needed):

- `main/src/Services/WiFi/WiFiCredentials.{h,cpp}`: length-checked credential copies and WPA2 validity rules.
- `main/src/Services/WiFi/WiFiBackoff.{h,cpp}`: the reconnect schedule (1 s doubling to a 60 s cap).

This is a separate ESP-IDF project built for the `linux` target with Unity. It compiles those sources straight from `main/src`, so the robot and the tests run the same code.

## Run

From this directory:

```sh
. ~/.espressif/tools/activate_idf_v5.5.3.sh
idf.py build && ./build/wifi_logic_test.elf
```

Source the activation script in the same shell as `idf.py`, because it defines `idf.py` as a shell function. The binary prints one line per test and exits with 0 when all pass and 1 otherwise. A first build takes a few seconds and rebuilds about one second. The `ld: warning: ignoring duplicate libraries` line on macOS is harmless.

## Adding tests

Pure-logic files must not include ESP-IDF, FreeRTOS, CMF or `secrets.h` headers. Add a new source file to the `SRCS` list in `main/CMakeLists.txt`. The generated `sdkconfig` and `build/` stay inside `build/`, so there's nothing to commit besides the sources.
