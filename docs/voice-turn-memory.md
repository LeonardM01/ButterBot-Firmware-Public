# Voice turn memory budget (#22)

What one "ask ButterBot anything" turn costs in internal RAM, PSRAM and flash, why it survives many turns in
one boot, and where the guard before the model load sits. The numbers come from the robot logs of #20
(`turn2.log`, `turn3.log`, `turn5.log`, `BBMEASURE` and `DEBUG-h3ap` lines), from the build, and from the
`VoiceTurn` heap lines of this firmware. Anything not measured is marked **est.**

"Internal" is `MALLOC_CAP_INTERNAL`, which includes the 16 KB `SPIRAM_MALLOC_RESERVE_INTERNAL` pool.
"Free" is `heap_caps_get_free_size`, "largest" is `heap_caps_get_largest_free_block`, "low" is the minimum
free size over a phase. All sizes are bytes.

## What broke in #20, and what changed

The #20 runs showed two failures:

1. **The network stalled with the models resident.** Internal free was about 10 KB during the download and
   dropped as low as 59 B. The download stalled after 3,991 B, and the next turns timed out on connect
   (`turn2.log`).
2. **The second turn of a boot rebooted while it reloaded AFE + MultiNet** (`turn5.log`): it started from
   27,907 free / 12,288 largest, and then `ALLOC FAIL size=32 caps=0x804 int_free=43`, a FreeRTOS
   internal allocation.

Every logged load drove internal RAM down to 59–319 B free, whatever it started from (27,268–28,703).
In `turn2.log` turns 2 and 3, the largest block was only 7,680 B, and the load kept 4.4 KB less internal
RAM (12,804 B against 16,504–17,180) but still ended at a low of 163. So the load is greedy. With
`SPIRAM_MALLOC_ALWAYSINTERNAL=256`, esp-sr's small `malloc()`s fill internal RAM first and only fall back to
PSRAM once it is gone. Whichever must-be-internal allocation comes next then fails.

The #22 firmware changes:

| Change | Internal RAM effect | Where |
|---|---|---|
| The ask worker's 12 KB stack and TCB are in PSRAM, created at boot. #20's DebugAsk worker had an 8 KB internal stack. | **+7,268 free and +8,192 largest** compared with #20's turns: 35,643 → 28,375 free, 26,624 → 18,432 largest when DebugAsk created its worker | `Net/AskWorker.cpp` |
| The capture buffer (320,044), AskClient and generator are allocated once at boot. | No per-turn allocation of long-lived objects, so they can't erode the largest block | `main.cpp`, `Audio/VoiceCapture.cpp` |
| During `loadModel()`, plain `malloc()` goes to PSRAM first (`heap_caps_malloc_extmem_enable(0)`, restored afterwards). | esp-sr's small allocations stop draining internal RAM. Explicit internal and DMA requests are unchanged. Effect **not yet measured**: see "Verify on the robot". | `Audio/AudioFrontend.cpp` |
| The models are unloaded in `ListenState`'s destructor, before `AskState` touches the network. | About 17 KB back before connect | `States/ListenState.cpp` |
| No `setVADParams()` during the turn. | No second AFE rebuild and no second load peak | – |
| A guard before `loadModel()`. | A low-memory turn is refused out loud instead of rebooting | `Util/VoiceTurnHeap.h` |

Not applied, pending measurements on the robot: shrinking internal task stacks (needs high-water marks
under Wi-Fi reconnect, BLE pairing and TLS, M1), moving the `AFE_feed`/`AFE_proc` stacks to PSRAM (needs the
AFE glitch meter), and lowering `SPIRAM_MALLOC_ALWAYSINTERNAL` permanently.

## Internal RAM per phase

Production #22 build. Columns: what the phase adds or frees, the expected free, and the source.
The baseline is `turn5.log`'s idle before DebugAsk created its worker: 35,643–35,679 free, 26,624 largest,
Wi-Fi online. That build also ran an instrument task with an internal stack of unknown size, so a
production build should have at least that much free.

| Phase | Internal consumers | Free (expected) | Source |
|---|---|---|---|
| Boot, services up | All internal task stacks, about 62 KB in total: wifi, BT, lwIP, esp_timer, BBStateMachine, app, Audio, AFE_feed/proc, FaceDet, cam, ModuleService…; Wi-Fi static RX/TX buffers; I2S DMA | 36,267 free, 26,624 largest (measured, t=3342 ms) | `turn5.log` `DEBUG-h3ap` |
| **Idle, Wi-Fi online** (turn start) | +AskWorker object (~250, **est.**) and VoiceCapture object (~24, **est.**), allocated at boot. Worker stack and TCB: 0, in PSRAM. Capture buffer: 0, in PSRAM | ≈ 35,300 free, ≈ 26,600 largest (**est.**: 35,643 − ~300 of boot objects; check `at=online`); see "Observed on the robot" | `VoiceTurn heap at=online` |
| Listening, models loaded | AFE + MultiNet: 16,504–17,180 steady with greedy small mallocs (measured). The explicit-internal share is ≤ 12,804 (**est.**, from `turn2.log` turns 2–3). The transient peak is unknown; it took everything that was free in #20. Recorder and capture: 0 | ≈ 22,500 free (**est.**, 35,300 − 12,804). The low must be checked on the robot | `VoiceTurn heap at=loaded`, `int_min` |
| Models unloaded | −AFE + MultiNet | Back to about idle | `turn5.log` `at=unloaded`: 28,055 after 28,375 idle |
| Network (connected, http) | esp_http_client struct and headers, esp-tls handle, lwIP netconn/pcb/semaphores, DNS: 1,748 to connect, 252 more to download, 2,000 total. RX 2,048 + TX 1,024 and the read buffer are PSRAM (>256 B or explicit caps). PcmStream and StreamingPcmSource objects (small) | About idle − 2,000 (**est.**) | `turn5.log` connected 26,307, downloaded 26,055 |
| Network over https (release) | mbedTLS buffers are PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`). The handshake runs on the PSRAM worker stack. Small TLS allocations: **unmeasured (#23)** | – | – |
| After the turn | Per turn, kept: 568 on the first turn of a boot, 252 in another boot, then 24 and 0 (one-time lazy allocations: lwIP per-thread semaphore, Wi-Fi BA sessions, stdio) | idle − ≤ 568 | `turn5.log`, `turn2.log` cleanup vs idle |

### Guard before the model load

`ListenState` refuses to load the models when idle internal RAM is below either threshold. It logs the
numbers, says the "thoughts got lost" line and goes back to Idle (decisions #9). The thresholds are in
`VoiceTurnHeap::ModelLoadMinIntFree` and `ModelLoadMinIntLargest`.

| Threshold | Value | Derived from | Margin |
|---|---|---|---|
| Internal free | **30,720** | The largest load seen took 28,444 (`turn2.log`: 28,703 idle → low 259). A load from 27,907 rebooted (`turn5.log` turn 2). | 2,276 over that load; 2,813 over the crash |
| Internal largest block | **7,680** | Loads succeeded from a 7,680 largest block (`turn2.log` turns 2–3). None were seen from lower. | – (equal to the lowest known-good) |
| Expected idle vs. the free threshold | ≈ 35,300 (**est.**) | 35,300 − 30,720 | **≈ 4,600 of headroom**: about 8 first-turn retentions (568) or about 190 later ones (24) |
| Expected idle vs. the largest threshold | ≈ 26,600 (**est.**) | 26,600 − 7,680 | **≈ 18,900 of headroom** |

The free threshold comes from loads that ran **without** the extmem window, so it is conservative. With the
window the load should need less internal RAM. Lower the threshold only once the `at=loaded` `int_min`
values from the robot are in.

## PSRAM per phase

8 MB, 8,123,088 free at boot, largest 7,995,392 (`turn5.log` t=3342 ms). PSRAM is not the constraint.

| Phase | PSRAM consumers | Free (expected) |
|---|---|---|
| Idle | Capture buffer 320,044 + AskWorker stack 12,288 + TCB (~350, **est.**), all at boot | ≈ 7,789,600 (**est.**: 8,122,324 measured idle − 332,682) |
| Listening, models loaded | AFE + MultiNet 3,520,380 (measured: 7,794,548 − 4,274,168). Up to about 17 KB more of esp-sr's small allocations under the extmem window (**est.**) | ≈ 4,250,000 |
| Network | Reply ring sized to the reply's PCM: 179,708–190,796 measured, 640 KB cap. HTTP RX/TX 3,072. Read buffer 2,048. TLS session and handshake ~40–50 KB (**est.**, #23). | ≥ 7,000,000 |
| After the turn | All freed. The largest block drops from 7.99 MB to 6.29 MB after the first turn: a small PSRAM allocation is left in the middle, harmless | ≈ idle |

## Flash

`factory` app partition: 8640 KiB = 8,847,360 B (`partitions.csv`). ESP-IDF 5.5.3, `idf.py build`, this branch:

| Build | `secrets.h` | App image | Free in `factory` |
|---|---|---|---|
| Dev (acceptance against the laptop) | `http://` URL, `BB_DEV_INSECURE 1`, `BB_DEBUG_ASK 0` | 8,626,288 | **221,072 (2.5 %)** |
| Release-like | `https://` URL, `BB_DEV_INSECURE 0`, `BB_DEBUG_ASK 0` | 8,697,168 | **150,192 (1.7 %)** |
| Measurement | `http://`, `BB_DEV_INSECURE 1`, `BB_DEBUG_ASK 1` | 8,640,224 | 207,136 (2.3 %), built one commit earlier: tens of bytes less than HEAD |

The release-like image links the ask client, `esp_http_client`, the mbedTLS TLS client and the full ESP-IDF CA
bundle. Pinning the ISRG roots (#24) should win back most of the 70,880 between the dev and release images.
The flash is fully partitioned, so no partition can grow.

**SPIFFS** (`storage`, 212 KiB = 217,088 B): 27 files, 74,808 B of content. #22 adds nothing. The filler,
offline and lost lines are flite text in rodata, a few hundred bytes (`Net/AskLines.cpp`). The measurement
build adds `debug/question.wav` (68,480 B), for 143,288 B.

## Verify on the robot

Each turn logs at most five `VoiceTurn` lines, plus one at boot:

```
I (…) VoiceTurn: heap at=online int_free=… int_min=… int_largest=… psram_free=… psram_largest=… guard_free=30720 guard_largest=7680
I (…) VoiceTurn: heap turn=N at=idle    int_free=… int_min=… int_largest=… psram_free=… psram_largest=…
I (…) VoiceTurn: heap turn=N at=loaded  …
I (…) VoiceTurn: heap turn=N at=network …     (questions only; "not_reached" when offline or cancelled before connecting)
I (…) VoiceTurn: heap turn=N at=after   …
```

`int_min` is the lowest internal free size since that turn's `at=idle` line. The lines carry sizes only:
no token, transcript or reply text. `AskWorker: stack min_free=…` follows every ask (M1). A refused load
logs `ListenState: Not loading the speech models: internal RAM free … (need 30720), largest block … (need 7680)`.

What to check across 5 or more turns in one boot, mixing questions, local commands and cancels (see
`docs/measurement-build.md` section 8 for the procedure):

1. `grep "VoiceTurn: heap turn=.* at=idle"`: `int_free` and `int_largest` stay within about 600 of turn 1's
   on every turn (one-time retention), and never fall towards 30,720 / 7,680.
2. `grep "at=loaded"`: `int_min` is well above zero. The #20 loads ended at 59–319. A value in that range
   means the extmem window has no effect, and the guard is the only protection.
3. `grep "at=network"`: `int_free` is about the idle value minus 2 KB. Not the ~10 KB of #20.
4. `grep -E "ALLOC FAIL|abort\(\)|Guru Meditation|rst:0x"`: nothing after boot.
5. `grep "Not loading the speech models"`: nothing (the guard should never fire under the budget).
6. `grep "AskWorker: stack min_free"`: stays above 4 KB (http measured 3,344 used).

## Observed on the robot

Not observed yet. The dev build above (commit `ab386d5`) was flashed to the robot over
`/dev/cu.usbserial-10`. After esptool's reset, the robot switched itself off within a second and the
USB-UART disappeared with it. That is the boot rule in `main.cpp`: unless the power button is held for
about 1 s at boot, it calls `ShutdownService::PowerOff()`. Nobody could hold the button, so no idle line
was captured. A panic or boot loop would have kept the port alive.

To fill this in: power on with a 1 s hold, start the capture (`docs/measurement-build.md` section 8 step 2),
and copy the `VoiceTurn: heap at=online` line and turn 1's `at=idle` line here. Replace the
**est.** idle values above with them, and recompute the guard headroom.

Static RAM (M12, release-like build against the `feat/22-ask-anything` base): DIRAM 155,879 → 156,731 B
(+852 B, mostly `esp_http_client` and mbedTLS statics that are now linked). App image 8,464,832 → 8,697,168 B
(+232,336 B; the base release didn't link the ask client at all).
