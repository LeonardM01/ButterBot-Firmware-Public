# Voice turn memory budget (#22)

This doc covers what one "ask ButterBot anything" turn costs in internal RAM, PSRAM and flash, what lets many turns
run in one boot, and what the guard before the model load can and can't catch. The numbers come from three
sources: the #20 robot logs (`turn2.log`, `turn3.log`, `turn5.log`, `BBMEASURE` and `DEBUG-h3ap` lines), four
question turns of this firmware on the robot ("Observed on the robot" below), and the build. Anything not
measured is marked **est.**

"Internal" means `MALLOC_CAP_INTERNAL`. It includes the 16 KB `SPIRAM_MALLOC_RESERVE_INTERNAL` pool, which
plain `malloc()` can't use. Only explicit internal, DMA or 8-bit requests can: FreeRTOS objects and stacks,
drivers, Wi-Fi. "Free" is `heap_caps_get_free_size` and "largest" is `heap_caps_get_largest_free_block`.
"Low" (`int_min`) is the minimum free size since the turn's `at=idle` line. All sizes are bytes.

## What broke in #20, and what changed

The #20 runs showed two failures:

1. **The network stalled with the models resident.** Internal free was about 10 KB during the download and
   dropped as low as 59 B. The download stalled after 3,991 B, and the next turns timed out on connect
   (`turn2.log`).
2. **The second turn of a boot rebooted while it reloaded AFE + MultiNet** (`turn5.log`). It started from
   27,907 free and 12,288 largest, then hit `ALLOC FAIL size=32 caps=0x804 int_free=43`, a FreeRTOS
   internal allocation.

Every #20 load drove internal RAM down to 59–319 B free, whatever it started from (27,268–28,703). In
`turn2.log` turns 2 and 3 the largest block was only 7,680 B, and the load kept 3.7–4.4 KB less internal RAM
(12,804 B against 16,504–17,180). It still ended at a low of 163. So the load was greedy. With
`SPIRAM_MALLOC_ALWAYSINTERNAL=256`, esp-sr's small `malloc()`s filled internal RAM first and fell back to PSRAM
only once it was gone. The next must-be-internal allocation then failed.

The #22 firmware changes:

| Change | Internal RAM effect | Where |
|---|---|---|
| The ask worker's 12 KB stack and TCB are in PSRAM, created at boot. #20's DebugAsk worker had an 8 KB internal stack. | Idle is 40,735–41,135 free, 25,600–26,624 largest (measured). #20 idled at 27,583–28,703 with its worker | `Net/AskWorker.cpp` |
| The capture buffer (320,044), AskClient and generator are allocated once at boot. | No per-turn allocation of long-lived objects to erode the largest block. Idle stays within 400 B over four turns | `main.cpp`, `Audio/VoiceCapture.cpp` |
| While the models load and the phrases register, plain `malloc()` goes to PSRAM first (`ExtmemFirst`). | The load costs 8,068–8,216 internal (measured), against draining to 59–319 B in #20. See "The extmem window" | `Audio/AudioFrontend.cpp` |
| The models are unloaded in `ListenState`'s destructor, before `AskState` touches the network. | The load's ~8 KB is back before connect | `States/ListenState.cpp` |
| No `setVADParams()` during the turn. | No second AFE rebuild and no second load peak | – |
| A guard before the turn: no recording while the ask worker still reads the buffer, and no load below the measured idle range. | A doubtful turn is refused out loud instead of corrupting the upload or rebooting | `Util/VoiceTurnGate.h` |

Not applied yet, pending measurements on the robot: shrinking internal task stacks (needs high-water marks
under Wi-Fi reconnect, BLE pairing and TLS, M1), moving the `AFE_feed`/`AFE_proc` stacks to PSRAM (needs the
AFE glitch meter), and lowering `SPIRAM_MALLOC_ALWAYSINTERNAL` permanently.

## The extmem window

`ExtmemFirst` calls `heap_caps_malloc_extmem_enable(0)` and restores `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` in
its destructor, so every way out of `loadModel()` and `setSpeechPhrases()` restores it. While it is alive,
`heap_caps_malloc_default()` tries PSRAM first for every size.

**Why esp-sr's own knob is not enough.** `afe_config_t::memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM` was
already set in every #20 load (`AudioFrontend.cpp` at `1153443`), and those loads still drained internal RAM.
The knob steers the AFE's own buffers only. MultiNet has no equivalent: `esp_mn_loader_mode_t` is about where
weights live. `nm -u` on the esp32s3 archives shows plain `malloc`/`calloc` imported by `libmultinet`,
`libvadnet`, `libfst`, `libflite_g2p`, `libdl_lib`, `libesp_audio_processor`, `libc_speech_features` and
`libhufzip`, alongside their explicit `heap_caps_*` calls. Only the global limit reaches those. The measured
result is the load's internal cost: 8,068–8,216 B, against everything that was free in #20.

**Why the window is global, and why that is safe for concurrently running code.** For the ~770 ms of the load,
plus the phrase registration, other tasks' plain `malloc()` lands in PSRAM too. These run at the same time:
the intro (AAC decode on the Audio task, flite on its PSRAM-stack task), I2S, Wi-Fi, lwIP, BLE, the LED and
the button ISR. None of them can rely on plain `malloc()` for DMA, ISR or cache-off memory:

- `heap_caps_malloc_default()` (`heap/heap_caps.c`) already falls back to PSRAM (`MALLOC_CAP_DEFAULT`) whenever
  internal RAM can't serve a ≤256 B request, and requests over 256 B go to PSRAM first anyway. Every #20 load
  ran internal RAM out, so every concurrent small `malloc()` in those loads already landed in PSRAM. The window
  makes that the rule for its duration instead of the accident.
- IDF's own components that need internal memory ask for caps: FreeRTOS objects and stacks (`pvPortMalloc`,
  `0x804`), I2S and GDMA buffers and descriptors (`MALLOC_CAP_DMA`), the Wi-Fi driver's DMA buffers. lwIP and
  Wi-Fi dynamic memory is already PSRAM-first (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`). The I2S channels and their DMA
  buffers are created at boot.
- This firmware's own IRAM ISRs (`ISRButtonInput::isr`, `BaseBoard::motorBoardISR`) only give a semaphore created
  at boot through `pvPortMalloc`, which asks for internal caps. They allocate nothing (M7) and read no heap memory
  allocated during a turn (M10). Its explicit allocations name their caps (M5, M6).

What it costs is speed: small allocations in PSRAM go through the cache. A side effect shows in PSRAM, under
"PSRAM per phase".

## Internal RAM per phase

Measured on the robot (four question turns, dev build over `http://`). The `listening`, `unloaded`, `playing`
and `downloaded` points are new and not observed yet.

| Point | What happened since the previous point | int_free | int_min | int_largest |
|---|---|---|---|---|
| `at=online` (boot) | Services up, Wi-Fi joined | 38,739 | 38,135 | 26,624 |
| `at=idle` | Turn starts | 40,735–41,135 | – | 25,600–26,624 |
| `at=loaded` | AFE + MultiNet in the window, the intro playing | 32,579–32,919 | ≥ 32,419 | 19,456–20,480 |
| `at=listening` | Phrases registered (window), intro finished, Speech on | not observed yet | | |
| `at=unloaded` | Recognition and recording, `setMode(Off)`, unload | not observed yet | | |
| `at=network` | Filler starts, the job is handed over, DNS + connect (worker's `AskPhase::Connected`, **before the upload**) | 37,319 (turn 1) | **3,019–3,659** | 24,576 (turn 1) |
| `at=playing`, `at=downloaded` | Upload, wait for the first byte, download | not observed yet | | |
| `at=after` | Reply played, turn over | ≈ 40,700–40,815 | – | 26,624 |

### The dip before the connect

Every turn's internal low, 3.0–3.7 KB free, happened **before `AskPhase::Connected`**. The `at=network` line is
taken when the request headers are sent, before any upload, so the upload and download bursts can't be the
cause. Between `at=loaded` and that point the turn ran:

1. the phrase registration (MultiNet's command FST: esp-sr, plain `malloc`);
2. the rest of the intro (flite synthesis: hundreds of small `calloc`s per sentence, `cst_alloc.c`);
3. recognition and recording in Speech mode (MultiNet detect, the AFE);
4. the unload;
5. the filler (flite again, concurrent with step 6);
6. the ask job: DNS, the TCP connect, `esp_http_client` setup (lwIP netconn semaphores and mailbox, FreeRTOS
   allocations).

What we know about it:

- It took 37,076–38,116 B from idle on every turn and bottomed out at a similar value whatever idle was, like
  #20's greedy load.
- Plain `malloc()` can't take the 16 KB reserve pool. To get below about 16 KB free, at least ~13 KB of the dip
  came from explicit internal, DMA or 8-bit requests: FreeRTOS objects or stacks, the Wi-Fi driver, lwIP's
  semaphores. Plain small allocations from flite and esp-sr (steps 1, 2, 5) can explain the rest down to the
  pool.
- It never failed. Nothing aborted, and at the low there were still 3 KB free. But it is the real margin of a
  turn, not the load.

What this firmware does about it:

- Step 1 now runs inside the extmem window, because the FST is built by the same esp-sr libraries as the load
  and stays allocated while listening. Whether that was a large part of the dip is **not measured yet**.
- `at=listening` and `at=unloaded` split steps 1–4 from 5–6. `int_min` is cumulative, so the first line whose
  `int_min` falls places the dip. If it falls only at `at=network`, the dip belongs to the filler and the
  connect. Next candidates there: route flite's allocator to PSRAM (`CST_USER_MALLOC`, `components/flite`)
  after checking synthesis speed, or start the filler after the connect.

### Guard before the turn

`ListenState` asks `VoiceTurnGate::check()` before it records or loads anything. It refuses in two cases,
logs the reason and says the "thoughts got lost" line, then goes back to Idle (decisions #9):

- `AskStillRunning`: the ask worker still reads the capture buffer, so a new recording would overwrite its
  upload (C7). AskState and DebugAsk wait the worker out before they leave, so this is a backstop.
- `LowInternalRam`: idle internal RAM is under `MinIntFree` or `MinIntLargest`.

| Threshold | Value | Derived from | Margin |
|---|---|---|---|
| Internal free | **38,912** | A turn took up to 38,116 from idle (41,135 → 3,019). | If that demand is fixed, a turn starting at 38,912 keeps 796. The lowest idle seen (40,735) is 1,823 above it. Idle varied by 400 over four turns |
| Internal largest block | **16,384** | The load carved up to 7,168 out of the largest block (26,624 → 19,456). | Over twice that. The lowest idle largest (25,600) is 9,216 above it |

What the guard can catch:

- internal RAM drifting below the range those four turns ran in: a leak, a lost boot allocation, a
  fragmented largest block;
- #20's reboot state (27,907 free, 12,288 largest), which both thresholds refuse.

What it can't catch:

- A turn whose own demand grows: a longer filler, a new path through the network code. It checks before the
  turn, not during it.
- A dip that is greedy rather than fixed. Then the threshold is conservative, and it may refuse turns that
  would have worked.
- States not measured yet: after local commands, cancels or RC, and with a phone connected over BLE (ANCS,
  AMS). If one of those idles below 38,912, the guard refuses every turn there and logs
  `Not loading the speech models: internal RAM free … (need 38912)`. Recalibrate from that state's `at=idle`
  and `int_min` lines rather than lowering the value blindly.

What really protects repeated turns is the bulleted list below, not the guard. The guard is the net under it.

- the PSRAM worker stack and the boot-time allocations: idle sits about 13 KB higher than in #20;
- the extmem window: the load costs about 8 KB instead of everything;
- unloading before the network: the dip happens with the models gone.

## PSRAM per phase

8 MB. 7,782,420 free at `at=online`, largest 7,733,248. PSRAM is not the constraint.

| Phase | PSRAM consumers | Free / largest (measured) |
|---|---|---|
| Idle | Capture buffer 320,044 + AskWorker stack 12,288 + TCB, all at boot | 7,782,420 / 7,733,248 (`at=online`) |
| Listening, models loaded | AFE + MultiNet 3,520,380 (#20), plus esp-sr's small allocations moved here by the window | 4,244,304 (turn 1) / 3,997,696 |
| Network | Reply ring sized to the reply's PCM (208,696–240,696 measured, 640 KB cap), HTTP RX/TX 3,072, read buffer 2,048, TLS ~40–50 KB (**est.**, #23) | – |
| After the turn | All freed but ~6 KB | 7,776,244 / **4,063,232** after turn 1, then 4,063,232 or 3,997,696 |

**The largest block halves after the first turn, and then stays put.** About 6 KB stays allocated in the middle
of PSRAM. The most likely cause is a long-lived allocation made by another task during the extmem window,
which landed between the model allocations (**est.**). It doesn't grow: turns 2–4 left the largest block at
4,063,232 or 3,997,696.

**A model load can't fail on that fragmentation.** The models are many allocations, not one:
- `mn7_en/mn7_index` lists 194 tensors totalling 2,739,172 B (all of `mn7_data`). The largest is 190,484 B
  (`embedding`, `conv1d4_w`).
- VADNet's 49 tensors are at most 32,788 B each.
- Even if MultiNet copied all of `mn7_data` in one piece, it would fit in the 3,997,696 B block.

A load would fail only if the largest block fell under about 200 KB. Watch `psram_largest` in the `at=idle`
lines.

## Flash

`factory` app partition: 8640 KiB = 8,847,360 B (`partitions.csv`). ESP-IDF 5.5.3, `idf.py build`, this
branch:

| Build | `secrets.h` | App image | Free in `factory` |
|---|---|---|---|
| Dev (acceptance against the laptop) | `http://` URL, `BB_DEV_INSECURE 1`, `BB_DEBUG_ASK 0` | 8,627,920 | **219,440 (2.5 %)** |
| Release-like | `https://` URL, `BB_DEV_INSECURE 0`, `BB_DEBUG_ASK 0` | 8,698,800 | **148,560 (1.7 %)** |
| Measurement | `http://`, `BB_DEV_INSECURE 1`, `BB_DEBUG_ASK 1` | 8,641,776 | 205,584 (2.3 %) |

The review fixes added 1,632 B to the dev and release images (`feat/22-ask-anything` was 8,626,288 and
8,697,168). Static RAM (M12, `idf.py size`): DIRAM 156,987 B in both configurations, 256 B over the base
branch's release-like 156,731.

The release-like image links the ask client, `esp_http_client`, the mbedTLS TLS client and the full ESP-IDF CA
bundle. Pinning the ISRG roots (#24) should win back most of the 70,880 between the dev and release images.
The flash is fully partitioned, so no partition can grow.

**SPIFFS** (`storage`, 212 KiB = 217,088 B): 27 files, 74,808 B of content. #22 adds nothing. The filler,
offline and lost lines are flite text in rodata, a few hundred bytes (`Net/AskLines.cpp`). The measurement
build adds `debug/question.wav` (68,480 B), for 143,288 B.

## Verify on the robot

Each turn logs up to eight `VoiceTurn` lines, plus one at boot:

```
I (…) VoiceTurn: heap at=online int_free=… int_min=… int_largest=… psram_free=… psram_largest=… guard_free=38912 guard_largest=16384
I (…) VoiceTurn: heap turn=N at=idle       int_free=… int_min=… int_largest=… psram_free=… psram_largest=…
I (…) VoiceTurn: heap turn=N at=loaded     …
I (…) VoiceTurn: heap turn=N at=listening  …
I (…) VoiceTurn: heap turn=N at=unloaded   …
I (…) VoiceTurn: heap turn=N at=network    …     (questions only; "not_reached" when offline or cancelled before connecting)
I (…) VoiceTurn: heap turn=N at=playing    …     (questions with a 200 reply)
I (…) VoiceTurn: heap turn=N at=downloaded …
I (…) VoiceTurn: heap turn=N at=after      …
```

The lines carry sizes only: no token, transcript or reply text. `AskWorker: stack min_free=…` follows every
ask (M1). A refused turn logs either `ListenState: Not listening: the last question is still being asked` or
`ListenState: Not loading the speech models: internal RAM free … (need 38912), largest block … (need 16384)`.

What to check across 5 or more turns in one boot, mixing questions, local commands and cancels (see
`docs/measurement-build.md` section 8 for the procedure):

1. `grep "VoiceTurn: heap turn=.* at=idle"`: `int_free` and `int_largest` stay within about 600 of turn 1's
   on every turn, and above 38,912 / 16,384. Note the idle value after local commands, cancels and RC, and
   with a phone connected.
2. `grep "at=loaded"`: `int_min` stays about 8 KB below idle (32.4–32.9 KB measured).
3. `grep -E "at=(listening|unloaded|network)"`: the first of them whose `int_min` drops to a few KB places the
   dip. Report it with the turn's numbers.
4. `grep -E "ALLOC FAIL|abort\(\)|Guru Meditation|rst:0x"`: nothing after boot.
5. `grep "Not loading the speech models\|Not listening"`: nothing, unless a state from point 1 idles under the
   threshold.
6. `grep "AskWorker: stack min_free"`: stays above 4 KB (9,532 measured over http; https is #23).
7. `grep "psram_largest"` in `at=idle`: stays above about 200 KB (about 4 MB measured).

## Observed on the robot

Dev build (`feat/22-ask-anything`, `http://`, `BB_DEV_INSECURE 1`), one boot, four consecutive question
turns. All four got HTTP 200. There was no reboot and no `ALLOC FAIL`. This was before the review fixes, so
the `listening`, `unloaded`, `playing` and `downloaded` points didn't exist yet, and the guard was 30,720 / 7,680.

| | Turn 1 | Turn 2 | Turn 3 | Turn 4 |
|---|---|---|---|---|
| `at=idle` int_free / largest | 41,135 / 26,624 | 40,767 / 26,624 | 40,899 / 25,600 | 40,735 / 26,624 |
| `at=loaded` int_free | 32,919 (min 32,747, largest 20,480) | 32,699 | 32,767 | 32,579 |
| `at=loaded` int_min, largest | ≥ 32,419, 19,456–20,480 on all turns | | | |
| `at=network` int_min | **3,019** | 3,655 | 3,527 | 3,659 |
| `at=after` int_free | 40,815 | ≈ 40.7–40.8 K | | |
| `at=after` psram_largest | 4,063,232 | 4,063,232 or 3,997,696 | | |
| AskWorker stack min_free | 9,532 of 12,288 on every turn | | | |

Turn 1's other values:
- `at=online`: 38,739 free, min 38,135, largest 26,624, PSRAM 7,782,420 / 7,733,248.
- `at=loaded`: PSRAM 4,244,304 free.
- `at=network`: 37,319 free, largest 24,576.
- `at=after`: PSRAM free 7,776,244.
- Network: connect 422 ms, upload 106,540 B in 4.9 s, first byte 3,269 ms, download 208,696 B in 5.7 s, 0 underruns.

Across turns, the upload ran at 19–27 KiB/s. Turn 2 had 7 underruns (2,153 ms starved) while downloading
240,696 B in 9.1 s, and the user heard it as "skewed". That is a network-throughput finding, not memory.

Not exercised yet: local commands, cancels, silence and 204, offline, RC, and https.
