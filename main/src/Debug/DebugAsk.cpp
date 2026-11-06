#include "DebugAsk.h"

#if BB_DEBUG_ASK_BUILD

#include <atomic>
#include <cstdio>
#include <optional>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Core/Application.h>
#include <Log/Log.h>
#include <Services/Audio/Audio.h>
#include "Audio/AudioFrontend.h"
#include "Audio/PcmPassthroughGen.h"
#include "Net/AskClient.h"
#include "Util/Elapsed.h"
#include "Util/ServiceLocator.h"
#include "MeasureFormat.h"

DEFINE_LOG(DebugAsk)

namespace {

constexpr char QuestionPath[] = "/spiffs/debug/question.wav"; // staged into SPIFFS by main/CMakeLists.txt
constexpr size_t CaptureBytes = 320044;  // 10 s of 16 kHz mono s16 plus a WAV header (#3)
constexpr uint32_t OnlineTimeoutMs = 20000;
constexpr uint32_t OnlinePollMs = 250;   // bounds abort latency while waiting for Wi-Fi
constexpr uint32_t IdlePollMs = 10;

// esp_http_client (and TLS on an https build) needs ~8 KB. Internal RAM, because the task reads SPIFFS.
constexpr uint32_t StackBytes = 8192;
constexpr UBaseType_t Priority = 5;      // below BBStateMachine (10), Audio (12) and lwIP (18)
constexpr BaseType_t Core = 0;           // off core 1, where Audio and the AFE run

struct HeapSnapshot {
	bool taken = false;
	size_t intFree, intMin, intLargest;
	size_t psFree, psMin, psLargest;
};

HeapSnapshot snapshot(){
	HeapSnapshot s;
	s.taken = true;
	s.intFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
	s.intMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
	s.intLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
	s.psFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	s.psMin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
	s.psLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
	return s;
}

/**
 * `min_since=turn` while heap_caps_monitor_local_minimum_free_size is running (the low point since
 * the turn started), `boot` otherwise.
 */
void printHeap(const char* at, const HeapSnapshot& s, bool turnMinimum){
	if(!s.taken){
		printf("BBMEASURE heap at=%s not_reached\n", at);
		return;
	}
	printf("BBMEASURE heap at=%s min_since=%s int_free=%u int_min=%u int_largest=%u psram_free=%u psram_min=%u psram_largest=%u\n",
		   at, turnMinimum ? "turn" : "boot",
		   (unsigned) s.intFree, (unsigned) s.intMin, (unsigned) s.intLargest,
		   (unsigned) s.psFree, (unsigned) s.psMin, (unsigned) s.psLargest);
}

void printNow(const char* at, bool turnMinimum){
	printHeap(at, snapshot(), turnMinimum);
}

class Worker {
public:
	bool start(){
		if(busy.exchange(true)) return false;
		// Armed here, before the worker waits for Wi-Fi or loads models, so an abort from now on is never lost.
		turn = AskClient::beginTurn();

		if(task == nullptr && xTaskCreatePinnedToCoreWithCaps(&Worker::taskMain, "DebugAsk", StackBytes, this, Priority, &task,
															  Core, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS){
			task = nullptr;
			busy = false;
			CMF_LOG(DebugAsk, LogLevel::Warning, "no internal RAM for the worker task");
			return false;
		}
		xTaskNotifyGive(task);
		return true;
	}

	bool running() const{ return busy; }

	void abort(){
		AskClient::abortInFlight();
	}

private:
	TaskHandle_t task = nullptr;
	std::atomic<bool> busy{false};
	uint32_t turn = 0; // written by start() before the worker is notified

	PcmPassthroughGen generator;
	std::optional<AskClient> askClient; // created once on the worker task, never destroyed

	// Filled by the AskClient phase hook; printed after the turn so UART output doesn't skew the timings.
	HeapSnapshot connected, playing, downloaded;

	static void taskMain(void* arg){
		auto* self = static_cast<Worker*>(arg);
		for(;;){
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			self->runTurn();
			self->busy = false;
		}
	}

	void onPhase(AskPhase phase){
		switch(phase){
			case AskPhase::Connected:
				connected = snapshot();
				break;
			case AskPhase::Playing:
				playing = snapshot();
				break;
			case AskPhase::Downloaded:
				downloaded = snapshot();
				break;
		}
	}

	bool stopRequested() const{
		return AskClient::aborted(turn);
	}

	bool waitOnline(){
		WiFiService* wifi = ServiceLocator::WiFiServiceInstance;
		if(wifi == nullptr) return false;
		for(uint32_t waited = 0; waited < OnlineTimeoutMs && !stopRequested(); waited += OnlinePollMs){
			if(wifi->waitOnline(OnlinePollMs)) return true;
		}
		return false;
	}

	/** Reads the canned question into `buf`. Returns its length, 0 on failure. */
	static size_t loadQuestion(uint8_t* buf, size_t cap){
		FILE* f = fopen(QuestionPath, "rb");
		if(f == nullptr){
			printf("BBMEASURE turn error=no_question_file path=%s\n", QuestionPath);
			return 0;
		}
		const size_t n = fread(buf, 1, cap, f);
		const bool tooBig = fgetc(f) != EOF;
		fclose(f);
		if(n == 0 || tooBig){
			printf("BBMEASURE turn error=question_unreadable_or_over_%u_B\n", (unsigned) cap);
			return 0;
		}
		return n;
	}

	void runTurn(){
		const int64_t tStart = nowUs();
		connected = playing = downloaded = HeapSnapshot{};

		// The Wi-Fi core is fixed by sdkconfig, so compare core 0 and 1 with two builds (docs/measurement-build.md).
#if CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1
		constexpr int WiFiCore = 1;
#else
		constexpr int WiFiCore = 0;
#endif
		printf("BBMEASURE turn start worker_core=%d wifi_core=%d lwip_max_sockets=%d capture_bytes=%u\n",
			   (int) xPortGetCoreID(), WiFiCore, CONFIG_LWIP_MAX_SOCKETS, (unsigned) CaptureBytes);
		printNow("idle", false);
		const bool monitoring = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;

		const Application* app = Application::getApp();
		Audio* audio = app->getService<Audio>();
		AudioFrontend* afe = app->getService<AudioFrontend>();
		if(audio == nullptr || afe == nullptr){
			printf("BBMEASURE turn error=missing_service\n");
		}else if(!waitOnline()){
			printf("BBMEASURE turn error=%s waited_ms=%u\n", stopRequested() ? "aborted" : "offline", (unsigned) msSince(tStart));
		}else{
			if(!askClient){
				askClient.emplace(*audio, generator, [this](AskPhase phase){ onPhase(phase); });
			}
			runLoadedTurn(*afe);
		}

		printf("BBMEASURE stack task=DebugAsk size=%u min_free=%u\n", (unsigned) StackBytes,
			   (unsigned) uxTaskGetStackHighWaterMark(nullptr));
		if(monitoring){
			heap_caps_monitor_local_minimum_free_size_stop();
		}
		printNow("cleanup", false);
		printf("BBMEASURE turn end total_ms=%u\n", (unsigned) msSince(tStart));
	}

	/** Steps 2-5: models resident, capture buffer, the ask, and cleanup. */
	void runLoadedTurn(AudioFrontend& afe){
		if(stopRequested()){
			printf("BBMEASURE turn error=aborted before_model_load\n");
			return;
		}

		const int64_t tLoad = nowUs();
		afe.loadModel(); // AFE + MultiNet, as ListenState does
		afe.setMode(AudioFrontend::Mode::VAD);
		const uint32_t loadMs = msSince(tLoad);

		auto* capture = static_cast<uint8_t*>(heap_caps_malloc(CaptureBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		if(capture == nullptr){
			printf("BBMEASURE turn error=no_psram_for_capture bytes=%u\n", (unsigned) CaptureBytes);
		}else{
			const size_t wavLen = loadQuestion(capture, CaptureBytes);
			printf("BBMEASURE turn model_load_ms=%u question_bytes=%u\n", (unsigned) loadMs, (unsigned) wavLen);
			printNow("loaded", true);

			if(wavLen > 0 && !stopRequested()){
				const AskResult r = askClient->ask(turn, capture, wavLen);
				printNow("after_turn", true);
				printHeap("connected", connected, true);
				printHeap("playing", playing, true);
				printHeap("downloaded", downloaded, true);

				char line[MeasureLineBytes];
				formatAskMeasure(line, sizeof(line), r);
				printf("%s\n", line);
			}
		}

		afe.setMode(AudioFrontend::Mode::Off);
		afe.unloadModel();
		heap_caps_free(capture);
	}
};

Worker worker;

}

bool DebugAsk::start(){
	return worker.start();
}

bool DebugAsk::running(){
	return worker.running();
}

void DebugAsk::abort(){
	worker.abort();
}

bool DebugAsk::waitIdle(uint32_t timeoutMs){
	const int64_t tStart = nowUs();
	while(worker.running()){
		if(msSince(tStart) >= timeoutMs) return false;
		vTaskDelay(pdMS_TO_TICKS(IdlePollMs));
	}
	return true;
}

#endif
