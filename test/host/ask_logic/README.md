# Ask client host tests

Unity tests for the pure logic behind the `/v1/ask` client. They build with the ESP-IDF `linux` target and run on the laptop, so they need no robot.

Covered code is in `main/src/Net/`:

- `AskOutcome`: maps the HTTP status to play, didn't-understand or thoughts-got-lost.
- `UrlPolicy`: `https://` always, `http://` only with `BB_DEV_INSECURE`; the host to resolve.
- `WavStreamParser`: checks the reply's WAV header as it streams in.
- `SampleAligner`: keeps 16-bit samples whole across network reads.
- `TurnAbort`: an abort sticks to the turn it was aimed at, even before `ask()` runs.

The firmware-only parts (`AskClient`, `StreamingPcmSource`) need the robot.

## Run

From this directory:

```sh
. ~/.espressif/tools/activate_idf_v5.5.3.sh
idf.py build && ./build/ask_logic_test.elf
```

The script defines `idf.py` as a shell function, so source it in the same shell. The binary prints one line per test, ends with `N Tests 0 Failures 0 Ignored` and `OK`, and exits 0. Any failure exits 1.

The first build takes a few seconds. The generated `sdkconfig` stays in `build/`, which git ignores.

## Add a test

1. Put the logic in `main/src/Net/` with no ESP-IDF, FreeRTOS, CMF or `secrets.h` includes.
2. Add the `.cpp` and a `test_*.cpp` to `main/CMakeLists.txt` here.
3. Call its `run…Tests()` from `main/test_main.cpp`.
