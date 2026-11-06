# Measurement build (#20)

A dev build that runs one `/v1/ask` turn on a button press and prints RAM, PSRAM and throughput numbers to serial. The results go in a comment on issue #20 and set the buffer sizes, socket count, task cores and MultiNet policy for the real voice turn.

Never ship this build: it has a debug trigger, and except for the HTTPS run in section 5 it talks plain HTTP.

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
| `heap at=connected` | connected and request headers sent. On the `https://` run (section 5) this is the TLS peak |
| `heap at=playing` | playback started, normally mid-download |
| `heap at=downloaded` | last byte read, socket still open |
| `heap at=after_turn` | playback done, models still loaded |
| `ask …` | outcome, status, `dns_ms`, `connect_ms` (TCP, plus the TLS handshake on `https://`), upload/download bytes, ms and KiB/s, first byte, first audio, playback `underruns` and `starved_ms` |
| `afe …` | audio-input glitches during the turn, see below |
| `stack task=DebugAsk` | worker stack size and its high-water mark (bytes never used) |
| `heap at=cleanup` | models unloaded, buffer freed; `min` is since boot again |

`int_*` is `MALLOC_CAP_INTERNAL`, `psram_*` is `MALLOC_CAP_SPIRAM`: free, minimum free and largest free block, in bytes. During the turn (`min_since=turn`) the minimum is the low point since the turn started. `AskClient` also prints its own `ask: HTTP …` line, and errors show as `BBMEASURE turn error=…`.

The `afe` line covers the AFE from VAD start until `ask()` returns, so it includes connect, upload, download and playback:

| Field | Meaning |
|---|---|
| `mic_overflows`, `mic_lost_ms` | The AFE feed task read the mic too late and the I2S driver dropped audio. Estimated from the time between reads against the 90 ms of mic DMA buffers, to within about 15 ms. |
| `mic_read_errors` | Mic reads that failed or came back short |
| `afe_backlog_max_ms` | Most audio fed to the AFE and not yet fetched. A steady turn stays at one or two 32 ms chunks |
| `afe_overflows` | The fetch task fell more than the AFE ring (50 chunks, 1.6 s) behind |
| `fetch_errors` | AFE fetches that failed or timed out |

Anything but zero in `mic_*`, `afe_overflows` or `fetch_errors` is a glitch the voice turn would hear.

Run at least three turns per configuration. Keep BLE connected (the phone app or the controller) so that Wi-Fi, BLE and the AFE all run at once.

## 4. Compare Wi-Fi on core 0 and core 1

The Wi-Fi task is pinned to core 0 by default, next to BLE, while Audio and the AFE run on core 1. For the comparison, switch it to core 1 for one build. Don't commit this change:

```sh
sed -i '' -e 's/^CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y$/# CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0 is not set/' \
          -e 's/^# CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1 is not set$/CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1=y/' sdkconfig
idf.py reconfigure && idf.py -p <PORT> flash monitor | tee turn-core1.log   # full rebuild, about 3 min
git checkout sdkconfig                                                        # back to core 0
```

`turn start` shows `wifi_core=1` when it worked. For each core, listen to the reply for clicks, dropouts or stutter, and compare `underruns` and `starved_ms` (a non-zero `underruns` means the download fell behind playback) and the `afe` line (input glitches next to Wi-Fi and BLE).

## 5. Follow-up: the HTTPS run (needs #23)

The laptop API speaks plain HTTP, so the runs above measure no TLS. The TLS numbers (handshake time, the heap peak at `connected`, input glitches during the handshake) come from one more run against the butterbot-api deployment on Hetzner once #23 is up. Until then, leave the `https` rows in the template empty and marked as a follow-up.

Only point the robot at that deployment. Never send the device token or audio to a third-party host (a public echo or test server, a tunnel service), not even for a quick TLS check.

1. Get a dev device token registered on the Hetzner deployment. Don't reuse the laptop's `dev.token`.
2. In `main/secrets.h`, set `BB_API_URL` to the deployment's `https://` base URL, `BB_DEVICE_TOKEN` to that token, `BB_DEV_INSECURE 0` (not needed for `https://`) and keep `BB_DEBUG_ASK 1`.
3. Flash and run at least three turns, as in section 3, with BLE connected.

The firmware checks the server certificate against ESP-IDF's full CA bundle until #24 pins the ISRG roots; hostname verification is on either way. A certificate or hostname failure shows as `ask: connect failed` and `outcome=lost status=-1`.

Reading the run:

- `connect_ms` is TCP plus the TLS handshake (DNS is in `dns_ms`). For the handshake alone, subtract one round trip: `ping` the API host from the same network and take the median.
- `heap at=connected` is the TLS peak. mbedTLS allocates from PSRAM in this build (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), so watch `psram_largest` as well as `int_min`.
- Compare the `afe` line with the HTTP runs: extra `mic_*` or a higher `afe_backlog_max_ms` points at the handshake's CPU time on core 0.
- The firmware gives DNS + connect 5 s (#7). A slower connect ends as `outcome=lost`, with `connected after … ms, past the 5000 ms deadline` in the `AskClient` log.

## 6. Afterwards

Put back a release `secrets.h` (`BB_DEV_INSECURE 0`, `BB_DEBUG_ASK 0`, an `https://` URL) and reflash, so the debug trigger and the canned WAV leave the robot.

## 7. Comment template for #20

```markdown
## Measurements: firmware <commit>, ESP-IDF 5.5.3, API on laptop (dev mode, http)

Setup: <router / distance>, BLE <connected to phone/controller/none>, reply <N> s, <N> turns per config.

### RAM (bytes; free / min / largest block)

| Point | Internal | PSRAM |
|---|---|---|
| Idle (before) | | |
| MultiNet + AFE + 320 KB capture | | |
| Connected (HTTP) | | |
| **Follow-up (#23):** connected, TLS peak on https | | |
| During download (playing) | | |
| After turn | | |
| After cleanup | | |
| Turn minimum (from `after_turn`) | | |

DebugAsk stack: <min_free> of 8192 B unused.

### Throughput and latency (median of <N>)

| | Wi-Fi core 0 | Wi-Fi core 1 |
|---|---|---|
| DNS / connect (ms) | | |
| Upload KiB/s (68 KB) | | |
| First byte (ms, includes server STT + LLM + TTS) | | |
| Download KiB/s | | |
| First audio (ms after upload) | | |
| Playback underruns / starved ms | | |
| Mic overflows / lost ms | | |
| AFE backlog max (ms) / overflows / fetch errors | | |
| Audible glitches | | |

### Follow-up (#23): HTTPS to the Hetzner API

Not measured yet: the laptop API is plain HTTP. To be filled in from the run in section 5 of `docs/measurement-build.md`, full CA bundle until #24.

| | Wi-Fi core <N> |
|---|---|
| DNS / connect incl. TLS (ms) | |
| TLS handshake (connect minus ping RTT, ms) | |
| Heap at connected: internal min / PSRAM largest | |
| Mic overflows / AFE backlog max during the turn | |

### Choices

- **Buffers:** capture <N> KB, reply ring <N> KB (min/max), prebuffer <N> KB, HTTP rx/tx <N>/<N> B.
- **`LWIP_MAX_SOCKETS`:** <N>, because …
- **Task cores:** Wi-Fi on core <N>, ask worker on core <N> (prio <N>, <N> KB stack), because …
- **MultiNet for cloud turns:** <keep resident / unload before upload>, because internal min was <N> and PSRAM largest block was <N>.
```
