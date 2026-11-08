#ifndef BUTTERBOT_FIRMWARE_ASKWORKER_H
#define BUTTERBOT_FIRMWARE_ASKWORKER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "AskClient.h"
#include "Audio/PcmPassthroughGen.h"
#include "Util/HeapSnapshot.h"

class Audio;

class WiFiService;

/**
 * The single task that runs /v1/ask turns (#22, decisions #12), shared by the voice turn and the
 * measurement build's debug turn.
 *
 * Created once at boot with its AskClient and pass-through generator, before any turn fragments internal RAM
 * (M8), and never deleted: its task loops for the whole runtime on `this`, so the destructor is deleted and
 * the task is never ended (M4 has no WithCaps object to free). Its stack and TCB are in PSRAM, so it costs no internal RAM: allowed by M3 because
 * it only ever runs AskClient::ask() (lwIP, esp_http_client, mbedTLS, Audio), never NVS, SPIFFS or a flash
 * write; callers read files on their own task and pass the bytes in.
 *
 * Ownership (C7): the job fields are written by submit() while the worker is idle and read by the worker
 * after it is notified; the result and phase snapshots are written by the worker and read by others once
 * busy() is false again (busy is the release/acquire point).
 */
class AskWorker {
public:
	/**
	 * M1, measured on the robot over http://: min_free 9,532 of 12,288 B (2,756 B used) on each of four voice
	 * turns (#22); #20's DebugAsk worker used 3,344 B of 8,192. TLS is not measured yet (#23): mbedTLS keeps its
	 * buffers on the heap (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC), and every turn logs `AskWorker: stack min_free=`,
	 * so the https sign-off comes from that line.
	 */
	static constexpr uint32_t StackBytes = 12 * 1024;
	static constexpr UBaseType_t Priority = 5;  // below BBStateMachine (10), Audio (12) and lwIP (18)
	static constexpr BaseType_t Core = 0;       // off core 1, where Audio and the AFE run

	/** Creates the task. Check ok(); without it every submit() fails and the voice turn says its thoughts got lost. */
	AskWorker(Audio& audio, const WiFiService* wifi);

	~AskWorker() = delete;

	AskWorker(const AskWorker&) = delete;

	AskWorker& operator=(const AskWorker&) = delete;

	AskWorker(AskWorker&&) = delete;

	AskWorker& operator=(AskWorker&&) = delete;

	bool ok() const{ return task != nullptr; }

	/**
	 * Starts `turn` (from AskClient::beginTurn()) with a whole WAV that stays valid until busy() is false.
	 * False when a turn is still running or there is no task.
	 */
	bool submit(uint32_t turn, const uint8_t* wav, size_t len);

	/** True from submit() until the turn has returned, playback included. */
	bool busy() const{ return running.load(std::memory_order_acquire); }

	/** Blocks up to `timeoutMs` until not busy. True if not busy. */
	bool waitIdle(uint32_t timeoutMs) const;

	struct PhaseHeap {
		HeapSnapshot connected, playing, downloaded;
	};

	/** The last turn's result and its heap at each AskPhase. Only while not busy. */
	const AskResult& result() const{ return last; }

	const PhaseHeap& phaseHeap() const{ return heap; }

	/**
	 * True once the reply is audible: Audio has started it, after any filler queued before it. False before,
	 * and again once playback has ended. Any task.
	 */
	bool replyAudible() const{ return generator.isOpen(); }

	/** Lowest free stack seen on the worker, in bytes (uxTaskGetStackHighWaterMark after each turn). */
	uint32_t stackMinFree() const{ return stackFree.load(); }

private:
	static constexpr uint32_t OnlinePollMs = 1000;

	PcmPassthroughGen generator;
	AskClient client;
	const WiFiService* const wifi;
	TaskHandle_t task = nullptr;

	std::atomic<bool> running{false};
	uint32_t turn = 0;
	const uint8_t* wav = nullptr;
	size_t len = 0;

	AskResult last;
	PhaseHeap heap;
	std::atomic<uint32_t> stackFree{StackBytes};

	static void taskMain(void* arg);

	void loop();

	void onPhase(AskPhase phase);
};

#endif //BUTTERBOT_FIRMWARE_ASKWORKER_H
