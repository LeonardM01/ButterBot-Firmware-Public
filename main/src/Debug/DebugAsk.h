#ifndef BUTTERBOT_FIRMWARE_DEBUGASK_H
#define BUTTERBOT_FIRMWARE_DEBUGASK_H

#if BB_DEBUG_ASK_BUILD

#include <cstdint>

/**
 * The measurement build's debug ask turn (#20). Compiled in only when secrets.h sets BB_DEBUG_ASK 1
 * (main/CMakeLists.txt turns that into BB_DEBUG_ASK_BUILD). See docs/measurement-build.md.
 *
 * Driven by DebugAskState on the state machine task, one step per poll():
 * 1. wait up to 20 s for home Wi-Fi;
 * 2. load the AFE and MultiNet and run the AFE in VAD mode, as a ListenState voice turn would, and read the
 *    canned question from SPIFFS into the boot-time capture buffer (on this task: SPIFFS needs an internal stack);
 * 3. stop the AFE and unload the models, as a voice turn does before the network;
 * 4. hand the question to the shared AskWorker (decisions #12), which POSTs it to /v1/ask and plays the reply.
 *
 * Heap snapshots, AskResult and the worker's stack high-water mark are printed as `BBMEASURE` lines.
 */
namespace DebugAsk {

/** Starts a turn. False if one is still running or a service, the worker or the capture buffer is missing. */
bool start();

/** Runs the next step. True while the turn is still running. State machine task only. */
bool poll();

/** True from start() until the turn has cleaned up. */
bool running();

/** Stops the turn in flight from any task; poll() ends it shortly after. */
void abort();

/**
 * Backstop for leaving early: aborts, waits until the worker has returned (warning every `warnEveryMs`), then
 * ends the turn. The abort is noticed within about 0.5 s, or after AskClient's connect deadline.
 */
void stop(uint32_t warnEveryMs);

}

#endif

#endif //BUTTERBOT_FIRMWARE_DEBUGASK_H
