# Ask client host tests

Unity tests for the pure logic behind the `/v1/ask` client and the voice turn (#22). They build with the ESP-IDF `linux` target and run on the laptop, so they need no robot.

Covered code is in `main/src/Net/` and `main/src/Audio/`:

- `AskOutcome`: maps the HTTP status to play, didn't-understand or thoughts-got-lost.
- `UrlPolicy`: `https://` always, `http://` only with `BB_DEV_INSECURE`; the host to resolve.
- `WavStreamParser`: checks the reply's WAV header as it streams in.
- `SampleAligner`: keeps 16-bit samples whole across network reads.
- `TurnAbort`: an abort sticks to the turn it was aimed at, even before `ask()` runs.
- `AskLines`: a finished turn (offline, aborted, 200, 200 cut short, 204, any other status, -1) to the line the robot says.
- `WavHeader`: the upload header for a recorded question, round-tripped through `WavStreamParser`.
- `UtteranceRecorder` (`main/src/Audio/`): records the question from speech onset to the first silence or the 320,000 B cap.

The firmware-only parts (`AskClient`, `AskWorker`, `StreamingPcmSource`, `VoiceCapture`, the states) need the robot.

## Run

From this directory:

```sh
. ~/.espressif/tools/activate_idf_v5.5.3.sh
idf.py build && ./build/ask_logic_test.elf
```

The script defines `idf.py` as a shell function, so source it in the same shell. The binary prints one line per test, ends with `N Tests 0 Failures 0 Ignored` and `OK`, and exits 0. Any failure exits 1.

The first build takes a few seconds. The generated `sdkconfig` stays in `build/`, which git ignores.

## Add a test

1. Put the logic in `main/src/Net/` (or `main/src/Audio/`) with no ESP-IDF, FreeRTOS, CMF or `secrets.h` includes.
2. Add the `.cpp` and a `test_*.cpp` to `main/CMakeLists.txt` here.
3. Call its `run…Tests()` from `main/test_main.cpp`.
