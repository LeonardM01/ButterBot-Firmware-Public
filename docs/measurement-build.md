# Measurement build (#20)

A dev build that runs one `/v1/ask` turn on a button press and prints RAM, PSRAM and throughput numbers to serial. The results go in a comment on issue #20 and set the buffer sizes, socket count, task cores and MultiNet policy for the real voice turn.

Never ship this build: it has a debug trigger, and except for the HTTPS run in section 5 it talks plain HTTP.

The real voice turn (#22) needs no measurement build: section 8 runs its on-robot acceptance checks with
`BB_DEBUG_ASK 0`.

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

Put the canned question at `debug/question.wav`. It is gitignored, so audio never goes into this repo. From the parent repo checkout:

```sh
cp ../butterbot-api/internal/ask/testdata/question.wav debug/question.wav
```

The build stops with an error if `BB_DEBUG_ASK` is 1 and the file is missing.

## 2. Start the API on the laptop

Follow "Run it on the laptop" in `butterbot-api/README.md` in the parent repo: `BB_DEV_MODE=1`, `BB_LISTEN_ADDR` on the LAN IP, and the dev token's hash in the devices file. Check it with the `curl` example there before you flash. If the macOS firewall is on, allow incoming connections for the Go binary.

## 3. Flash, trigger, watch

```sh
. ~/.espressif/tools/activate_idf_v5.5.3.sh
idf.py -p <PORT> flash monitor | tee turn.log
```

`flash` also writes the storage SPIFFS image with `/spiffs/debug/question.wav` in it (the file you copied in step 1).

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
| `heap at=loaded` | MultiNet resident, AFE in VAD mode, question read into the 320 KB capture buffer (allocated at boot) |
| `heap at=unloaded` | AFE stopped and models unloaded, as a voice turn does before the network |
| `heap at=connected` | connected and request headers sent. On the `https://` run (section 5) this is the TLS peak |
| `heap at=playing` | playback started, normally mid-download |
| `heap at=downloaded` | last byte read, socket still open |
| `heap at=after_turn` | playback done |
| `ask …` | outcome, status, `dns_ms`, `connect_ms` (TCP, plus the TLS handshake on `https://`), upload/download bytes, ms and KiB/s, first byte, first audio, playback `underruns` and `starved_ms` |
| `afe …` | audio-input glitches during the turn, see below |
| `stack task=AskWorker` | the shared ask worker's stack size (12 KB, PSRAM) and its high-water mark (bytes never used) |
| `heap at=cleanup` | turn over; `min` is since boot again |

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

AskWorker stack: <min_free> of 12288 B unused.

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

## 8. Voice turn acceptance on the robot (#22, checklist items 4–10 of #15)

No measurement build here: this tests the shipped voice turn against the laptop API.

1. `main/secrets.h` as in section 1, but with `#define BB_DEBUG_ASK 0`. Keep `BB_DEV_INSECURE 1` and the
   `http://<laptop LAN IP>:8080` URL. Start the API as in section 2.
2. Flash and log the whole session to a file outside the repo. `idf.py monitor` needs a TTY, so either run it
   in a terminal:

   ```sh
   . ~/.espressif/tools/activate_idf_v5.5.3.sh
   idf.py -p <PORT> flash monitor | tee ~/butterbot-logs/voice-$(date +%H%M).log
   ```

   or, without a TTY, flash and then capture with the IDF Python's pyserial (`-I` keeps it isolated). The
   USB-UART only exists while the robot is powered, and a reset pulse switches it off unless the power button
   is held, so this waits for the port and opens it without touching DTR/RTS. Save it outside the repo as
   `~/butterbot-logs/capture.py`:

   ```python
   import os, sys, time, serial
   port, out, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
   end = time.time() + seconds
   with open(out, "ab") as f:
       while time.time() < end:
           if not os.path.exists(port):
               time.sleep(0.2); continue
           s = serial.Serial(); s.port, s.baudrate, s.timeout = port, 115200, 0.5
           s.dtr = False; s.rts = False          # before open(): no reset pulse
           try:
               s.open()
               while time.time() < end:
                   if chunk := s.read(4096):
                       f.write(chunk); f.flush()
           except (serial.SerialException, OSError):
               pass                              # robot switched off; wait for it
           finally:
               s.close()
   ```

   ```sh
   . ~/.espressif/tools/activate_idf_v5.5.3.sh
   idf.py -p <PORT> flash          # the robot switches off after the flash: that's the boot hold rule
   python -I ~/butterbot-logs/capture.py <PORT> ~/butterbot-logs/voice.log 1200 &
   ```

   Then switch the robot on with the usual 1 s power-button hold. The last argument is how long to record,
   in seconds (1200 s = 20 min). The first boot lines can be missed while USB enumerates; the `VoiceTurn`
   lines come later.
3. Wait for `Wi-Fi online.` and the `VoiceTurn: heap at=online` line, then run the checks below in one boot,
   in this order. A short press (0.5 s or less) or the controller's Listen starts a turn.

| # | Do | Expect | Grep |
|---|---|---|---|
| 4 | Ask a built-in command ("tell me a joke"). Repeat with the router off. | The local routine, both times. No upload | that turn has `at=idle`, `at=loaded`, `at=listening`, `at=unloaded`, `at=after` and no `at=network`; no `Listen result` |
| 5 | Ask a question ("what is the capital of France"). | Filler line with fast LED blink. The reply starts before the download ends (`first audio` < `down … ms`). Back to Idle | `ask: … question handed over`, `AskClient: ask: HTTP 200`, `AskWorker: stack min_free` |
| 6 | Ask, then press the power button while the filler or reply plays. Again with the controller's ShutUp, and once while it is still connecting. | Stops at once and says "OK"/"Got it". The robot is back in Idle within about 0.5 s (connecting: up to the 5 s budget) | `AskClient: ask: … aborted` or `HTTP -1`; no reply after the abort phrase |
| 6b | Ask, then enter RC from the controller while it thinks. | Silent stop, then RC mode | `AskWorker: stack min_free`, then `WiFiService`'s switch to the controller network |
| 7 | Router off (or `BB_API_URL` host unreachable), wait for `Wi-Fi offline`, then ask. | The offline line ("My brain is out of Wi-Fi range"…). Local commands still work | `ask: offline` |
| 8 | Stop the API and ask. Then start it, pause it with `kill -STOP <api pid>` (it still accepts the connection but never answers) and ask; `kill -CONT` afterwards. | "My thoughts got lost…" both times | `connect failed`; then `no reply within 12000 ms` (or `upload failed` if the paused server's socket buffer fills) |
| 9 | Press and say nothing. | "I did not understand that", no upload. (The decision: no-speech is local, a server 204 says the same line.) | `question=0, recorded 0 B` |
| 10 | Ask one question that runs past 10 s. | Recording stops at 10 s, the question is asked anyway | `ask: 320044 B question handed over` |

4. Memory across turns: run at least 5 consecutive turns in that boot, mixing 4, 5, 6 and 9. Then:

   ```sh
   L=~/butterbot-logs/voice.log
   grep -a "VoiceTurn: heap" $L                       # all phases of every turn
   grep -a "VoiceTurn: heap turn=.* at=idle" $L       # within ~600 B of turn 1, above guard_free/guard_largest
   grep -a "at=loaded" $L                             # int_min about 8 KB under idle (32.4-32.9 KB measured)
   grep -aE "at=(listening|unloaded|network)" $L      # the first whose int_min drops to a few KB places the dip
   grep -a "AskWorker: stack min_free" $L             # > 4096 (9,532 measured over http)
   grep -aE "ALLOC FAIL|abort\(\)|Guru Meditation|rst:0x|Not loading the speech models|Not listening" $L   # nothing after boot
   ```

   What each value should look like, and why, is in `docs/voice-turn-memory.md`.
5. Privacy (checklist 12): `grep -aiE "bearer|token|transcript" $L` shows only the boot's insecure-build warning.
