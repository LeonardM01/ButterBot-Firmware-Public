#ifndef BUTTERBOT_FIRMWARE_DEBUGASK_H
#define BUTTERBOT_FIRMWARE_DEBUGASK_H

#if BB_DEBUG_ASK_BUILD

#include <cstdint>

/**
 * The measurement build's debug ask turn (#20). Compiled in only when secrets.h sets BB_DEBUG_ASK 1
 * (main/CMakeLists.txt turns that into BB_DEBUG_ASK_BUILD). See docs/measurement-build.md.
 *
 * One turn, on a worker task created on first use and kept for the runtime (core 0, internal stack):
 * 1. wait for home Wi-Fi;
 * 2. load the AFE and MultiNet and run the AFE in VAD mode, as a ListenState voice turn would;
 * 3. allocate a 320 KB capture buffer in PSRAM and read the canned question from SPIFFS into it;
 * 4. POST it to /v1/ask and play the streamed reply with a pass-through generator;
 * 5. stop the AFE, unload the models and free the buffer.
 *
 * Heap snapshots, AskResult and the task's stack high-water mark are printed as `BBMEASURE` lines.
 */
namespace DebugAsk {

/** Starts a turn. False if one is still running or the worker task could not be created. */
bool start();

/** True from start() until the turn has cleaned up. */
bool running();

/** Stops the turn in flight from any task; it cleans up and ends shortly after. */
void abort();

/** Blocks up to `timeoutMs` until no turn is running. True if none is. */
bool waitIdle(uint32_t timeoutMs);

}

#endif

#endif //BUTTERBOT_FIRMWARE_DEBUGASK_H
