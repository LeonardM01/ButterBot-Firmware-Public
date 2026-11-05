# ButterBot Firmware

## Building

To build the ButterBot firmware, you'll need the ESP-IDF. You can find the getting started
guide [here](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/).
The production firmware is built using IDF version 5.5.3.

All required components are contained in the repository, and managed components (esp-sr,
esp-dl, esp-tflite-micro, face detection and recognition models, etc.) are fetched
automatically by the IDF component manager during the build.

In the root directory of the project:

Before configuring or building, create your local credentials header:

```shell
cp main/secrets.h.example main/secrets.h
```

Replace every placeholder in `main/secrets.h`. It contains your Wi-Fi credentials and
device token, is intentionally ignored by Git, and must never be committed. Set
`BB_DEV_INSECURE` to `1` only for a local development API over `http://`; keep it `0`
for release builds using `https://`.

**To build the firmware** run ```idf.py build```

**To upload the firmware to the device** run ```idf.py -p <PORT> flash```. Replace `<PORT>` with
the port the robot is attached to, for ex. ```COM6``` or ```/dev/ttyACM0```.

Flashing also uploads the data partitions: the SPIFFS filesystem image built from
[spiffs](spiffs), the object detection model from [models](models), and the speech
recognition and face detection / recognition models provided by the managed components.

## Restoring the stock firmware

To restore the stock firmware, you can download the prebuilt binary on
the [releases page](https://github.com/CircuitMess/ButterBot-Firmware-Public/releases) of
this repository and flash it manually using esptool:

```shell
esptool -c esp32s3 -b 921600 -p <PORT> write_flash 0 ButterBot-Firmware.bin
```

Alternatively, you can also do so using [CircuitBlocks](https://code.circuitmess.com/) by
logging in, clicking the "Restore Firmware" button in the top-right corner, and following the
on-screen instructions.

## Syncing this fork with CircuitMess

Keep this fork aligned with the upstream firmware before starting new work. In a clone of
this fork, add CircuitMess as the `upstream` remote once, then fetch and merge its `master`
branch:

```shell
git remote add upstream https://github.com/CircuitMess/ButterBot-Firmware-Public.git
git fetch upstream
git switch master
git merge upstream/master
```

Resolve and test any conflicts before pushing. You can also use GitHub's **Sync fork** button.
Do not use an upstream sync to add `main/secrets.h`: local secrets stay untracked and are never
included in a commit or pull request.

## Flash budget

The 16 MB flash is fully partitioned, so new features (Wi-Fi, TLS, HTTP client) must fit in
the `factory` app partition (8640 KB). Measured with ESP-IDF 5.5.3:

| Build | App image | Free in `factory` |
|---|---|---|
| Upstream config | 8,627,632 B | 214 KB (2%) |
| Current config: max log level Info, silent assertions | 8,415,088 B | 421 KB (5%) |
| Current config + `CONFIG_COMPILER_OPTIMIZATION_SIZE` (`-Os`) | 8,151,520 B | 680 KB (8%) |

Debug-level log calls are compiled out (they never print at the default Info level), and
failed assertions abort without printing file and line.

If more room is needed, these are the remaining levers, in order of cost:

1. **Compile for size (`-Os`)**: about 260 KB more. Slows our own code slightly; the prebuilt
   esp-sr and esp-dl libraries are unaffected. Test on the robot before keeping it.
2. **Smaller flite voice**: the on-device TTS (`components/flite`) is about 4.6 MB of the app:
   the `cmu_us_kal16` voice (about 3.9 MB) plus the CMU lexicon (about 0.56 MB). Its size does
   not depend on how many phrases exist, so trimming phrases saves nothing. Switching to the
   8 kHz `cmu_us_kal` voice should save roughly 2 MB at lower audio quality (estimate, not yet
   measured).
3. **Remove flite entirely**: frees about 4.6 MB, but every spoken phrase then has to come from
   butterbot-api. The robot goes silent without Wi-Fi or the server (including the intruder
   alert), each phrase waits on the network, and the current time and taught IR command names
   (spoken from phonemes) need server-side TTS too. Pre-recording the ~690 phrases instead
   does not help: about 30 minutes of speech takes more space than flite.
