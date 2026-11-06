# Measurement logic host tests

Unity tests for the pure logic of the measurement build (#20). They build with the ESP-IDF `linux` target and run on the laptop, so they need no robot.

Covered code is in `main/src/Debug/`:

- `PowerPress`: reads a power-button hold in Idle as short (listen), medium (debug ask turn) or ignored.

The debug turn itself (`DebugAsk`, `DebugAskState`) needs the robot; see `docs/measurement-build.md`.

## Run

From this directory:

```sh
. ~/.espressif/tools/activate_idf_v5.5.3.sh
idf.py build && ./build/measure_logic_test.elf
```

Source the script in the same shell, because it defines `idf.py` as a shell function. The binary ends with `N Tests 0 Failures 0 Ignored` and `OK` and exits 0; any failure exits 1. The generated `sdkconfig` stays in `build/`, which git ignores.
