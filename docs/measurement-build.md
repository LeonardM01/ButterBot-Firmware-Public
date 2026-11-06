# Measurement build (#20)

A dev build that runs one `/v1/ask` turn on a button press and prints RAM, PSRAM and throughput numbers to serial. The results go in a comment on issue #20 and set the buffer sizes, socket count, task cores and MultiNet policy for the real voice turn.

Never ship this build: it talks plain HTTP and has a debug trigger.

## 1. Set up `main/secrets.h`

Start from `main/secrets.h.example`:

```c
#define BB_WIFI_SSID "<home network>"
#define BB_WIFI_PASSWORD "<home password>"
#define BB_API_URL "http://<laptop LAN IP>:8080"   // no trailing slash
#define BB_DEVICE_TOKEN "<contents of dev.token>"
#define BB_DEV_INSECURE 1                           // allows http://, warns at boot and on every ask
#define BB_DEBUG_ASK 1                              // compiles in the trigger, adds the canned WAV to SPIFFS
```

Use the dev token, never a production one. Find the laptop IP with `ipconfig getifaddr en0`. The configure step prints a `BB_DEBUG_ASK is 1` warning, which confirms the switch was read.

## 2. Start the API on the laptop

Follow "Run it on the laptop" in `butterbot-api/README.md` in the parent repo: `BB_DEV_MODE=1`, `BB_LISTEN_ADDR` on the LAN IP, and the dev token's hash in the devices file. Check it with the `curl` example there before you flash. If the macOS firewall is on, allow incoming connections for the Go binary.

## 3. Flash, trigger, watch

```sh
. ~/.espressif/tools/activate_idf_v5.5.3.sh
idf.py -p <PORT> flash monitor | tee turn.log
```

`flash` also writes the storage SPIFFS image with `/spiffs/debug/question.wav` in it (68 KB, synthetic "question" from butterbot-api's testdata).

1. Wait for the `Wi-Fi online.` warning.
2. Leave the robot in Idle with no routine running. Hold the power button for **1 to 2.5 s**, then release. A tap (0.5 s or less) still listens as usual, and a 3 s hold still shuts down.
3. The listen LED comes on. The robot loads the models, uploads, and plays the reply. The LED goes off when it's back in Idle.
4. A power press during the turn aborts it, and so does entering RC mode.

Pull the numbers out with `grep BBMEASURE turn.log`. One turn prints, in order:

| Line | What it is |
|---|---|
| `turn start` | worker core, Wi-Fi task core, `LWIP_MAX_SOCKETS`, capture buffer size |
| `heap at=idle` | before the turn; `min` is since boot |
| `turn model_load_ms=… question_bytes=…` | AFE + MultiNet load time |
| `heap at=loaded` | MultiNet resident, AFE in VAD mode, 320 KB capture buffer allocated |
| `heap at=connected` | connected and request sent: the TLS peak on an `https://` URL |
| `heap at=playing` | playback started, normally mid-download |
| `heap at=downloaded` | last byte read, socket still open |
| `heap at=after_turn` | playback done, models still loaded |
| `ask …` | outcome, status, upload/download bytes, ms and KiB/s, first byte, first audio, underruns, starved ms |
| `stack task=DebugAsk` | worker stack size and its high-water mark (bytes never used) |
| `heap at=cleanup` | models unloaded, buffer freed; `min` is since boot again |

`int_*` is `MALLOC_CAP_INTERNAL`, `psram_*` is `MALLOC_CAP_SPIRAM`: free, minimum free and largest free block, in bytes. During the turn (`min_since=turn`) the minimum is the low point since the turn started. `AskClient` also prints its own `ask: HTTP …` line, and errors show as `BBMEASURE turn error=…`.

Run at least three turns per configuration. Keep BLE connected (the phone app or the controller) so that Wi-Fi, BLE and the AFE all run at once.

## 4. Compare Wi-Fi on core 0 and core 1

The Wi-Fi task is pinned to core 0 by default, next to BLE, while Audio and the AFE run on core 1. For the comparison, switch it to core 1 for one build. Don't commit this change:

```sh
sed -i '' -e 's/^CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y$/# CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0 is not set/' \
          -e 's/^# CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1 is not set$/CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1=y/' sdkconfig
idf.py reconfigure && idf.py -p <PORT> flash monitor | tee turn-core1.log   # full rebuild, about 3 min
git checkout sdkconfig                                                        # back to core 0
```

`turn start` shows `wifi_core=1` when it worked. For each core, listen to the reply for clicks, dropouts or stutter, and compare `underruns` and `starved_ms`. A non-zero `underruns` means the download fell behind playback.

## 5. Afterwards

Put back a release `secrets.h` (`BB_DEV_INSECURE 0`, `BB_DEBUG_ASK 0`, an `https://` URL) and reflash, so the debug trigger and the canned WAV leave the robot.

## 6. Comment template for #20

```markdown
## Measurements: firmware <commit>, ESP-IDF 5.5.3, API on laptop (dev mode, http)

Setup: <router / distance>, BLE <connected to phone/controller/none>, reply <N> s, <N> turns per config.

### RAM (bytes; free / min / largest block)

| Point | Internal | PSRAM |
|---|---|---|
| Idle (before) | | |
| MultiNet + AFE + 320 KB capture | | |
| Connected (TLS/HTTP peak) | | |
| During download (playing) | | |
| After turn | | |
| After cleanup | | |
| Turn minimum (from `after_turn`) | | |

DebugAsk stack: <min_free> of 8192 B unused.

### Throughput and latency (median of <N>)

| | Wi-Fi core 0 | Wi-Fi core 1 |
|---|---|---|
| Upload KiB/s (68 KB) | | |
| First byte (ms, includes server STT + LLM + TTS) | | |
| Download KiB/s | | |
| First audio (ms after upload) | | |
| Underruns / starved ms | | |
| Audible glitches | | |

### Choices

- **Buffers:** capture <N> KB, reply ring <N> KB (min/max), prebuffer <N> KB, HTTP rx/tx <N>/<N> B.
- **`LWIP_MAX_SOCKETS`:** <N>, because …
- **Task cores:** Wi-Fi on core <N>, ask worker on core <N> (prio <N>, <N> KB stack), because …
- **MultiNet for cloud turns:** <keep resident / unload before upload>, because internal min was <N> and PSRAM largest block was <N>.
```
