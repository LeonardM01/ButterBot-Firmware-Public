#include "DebugAsk.h"

#if BB_DEBUG_ASK_BUILD

#include <cstdio>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Core/Application.h>
#include <Log/Log.h>
#include <Services/Audio/Audio.h>
#include "Audio/AudioFrontend.h"
#include "Net/AskClient.h"
#include "Util/Elapsed.h"
#include "Util/HeapSnapshot.h"
#include "Util/ServiceLocator.h"
#include "MeasureFormat.h"

DEFINE_LOG(DebugAsk)

namespace {

constexpr char QuestionPath[] = "/spiffs/debug/question.wav"; // staged into SPIFFS by main/CMakeLists.txt
constexpr uint32_t OnlineTimeoutMs = 20000;

enum class Step : uint8_t {
	Idle,
	WaitOnline,
	Asking,
};

// State machine task only (C7), except `turn`, which abort() never touches (it aborts whatever is armed).
struct Turn {
	Step step = Step::Idle;
	uint32_t id = 0;
	int64_t tStart = 0;
	bool monitoring = false;
	AfeGlitchMeter::Stats glitches{};
} turn;

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
	printHeap(at, takeHeapSnapshot(), turnMinimum);
}

bool stopRequested(){
	return AskClient::aborted(turn.id);
}

/** Reads the canned question into `buf`. Returns its length, 0 on failure. SPIFFS: internal-stack task only (M3). */
size_t loadQuestion(uint8_t* buf, size_t cap){
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

void finish(){
	if(AskWorker* worker = ServiceLocator::AskWorkerInstance.get()){
		printf("BBMEASURE stack task=AskWorker size=%u min_free=%u\n", (unsigned) AskWorker::StackBytes, (unsigned) worker->stackMinFree());
	}
	if(turn.monitoring){
		heap_caps_monitor_local_minimum_free_size_stop();
		turn.monitoring = false;
	}
	printNow("cleanup", false);
	printf("BBMEASURE turn end total_ms=%u\n", (unsigned) msSince(turn.tStart));
	turn.step = Step::Idle;
}

/**
 * Steps 2-4, blocking the state machine for about a second like a ListenState model load does.
 * Ends the turn itself unless the worker took the question.
 */
void prepareAndSubmit(){
	AudioFrontend* afe = Application::getApp()->getService<AudioFrontend>();
	VoiceCapture* capture = ServiceLocator::VoiceCaptureInstance.get();
	AskWorker* worker = ServiceLocator::AskWorkerInstance.get();
	if(afe == nullptr || capture == nullptr || worker == nullptr){
		printf("BBMEASURE turn error=missing_service\n");
		finish();
		return;
	}
	if(worker->busy()){
		// It still reads the capture buffer (C7). Only this task submits, so it stays idle once seen idle here.
		printf("BBMEASURE turn error=worker_busy\n");
		finish();
		return;
	}

	const int64_t tLoad = nowUs();
	if(!afe->loadModel()){ // AFE + MultiNet, as ListenState does
		printf("BBMEASURE turn error=model_load_failed\n");
		finish();
		return;
	}
	afe->resetGlitchMeter();
	afe->setMode(AudioFrontend::Mode::VAD);
	const uint32_t loadMs = msSince(tLoad);

	// The boot-time capture buffer, as a voice turn uses it: no per-turn allocation.
	const std::span<uint8_t> buf = capture->raw();
	const size_t wavLen = loadQuestion(buf.data(), buf.size());
	printf("BBMEASURE turn model_load_ms=%u question_bytes=%u\n", (unsigned) loadMs, (unsigned) wavLen);
	printNow("loaded", true);

	// Cloud turn shape (#20 finding): with AFE + MultiNet resident too little internal RAM is left for Wi-Fi RX.
	turn.glitches = afe->glitchStats();
	afe->setMode(AudioFrontend::Mode::Off);
	afe->unloadModel();
	printNow("unloaded", true);

	if(wavLen == 0 || stopRequested() || !worker->submit(turn.id, buf.data(), wavLen)){
		if(stopRequested()) printf("BBMEASURE turn error=aborted before_ask\n");
		finish();
		return;
	}
	turn.step = Step::Asking;
}

void reportAsk(const AskWorker& worker){
	printNow("after_turn", true);
	printHeap("connected", worker.phaseHeap().connected, true);
	printHeap("playing", worker.phaseHeap().playing, true);
	printHeap("downloaded", worker.phaseHeap().downloaded, true);

	char line[MeasureLineBytes];
	formatAskMeasure(line, sizeof(line), worker.result());
	printf("%s\n", line);
	formatAfeMeasure(line, sizeof(line), turn.glitches);
	printf("%s\n", line);
}

}

bool DebugAsk::start(){
	if(turn.step != Step::Idle) return false;
	if(!ServiceLocator::AskWorkerInstance || !ServiceLocator::AskWorkerInstance->ok() || !ServiceLocator::VoiceCaptureInstance
	   || ServiceLocator::WiFiServiceInstance == nullptr){
		CMF_LOG(DebugAsk, LogLevel::Warning, "no ask worker, capture buffer or Wi-Fi service");
		return false;
	}

	// Armed here, before waiting for Wi-Fi or loading models, so an abort from now on is never lost.
	turn = Turn{};
	turn.id = AskClient::beginTurn();
	turn.tStart = nowUs();

	// The Wi-Fi core is fixed by sdkconfig, so compare core 0 and 1 with two builds (docs/measurement-build.md).
#if CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1
	constexpr int WiFiCore = 1;
#else
	constexpr int WiFiCore = 0;
#endif
	printf("BBMEASURE turn start worker_core=%d wifi_core=%d lwip_max_sockets=%d capture_bytes=%u\n",
		   (int) AskWorker::Core, WiFiCore, CONFIG_LWIP_MAX_SOCKETS, (unsigned) VoiceCapture::BufferBytes);
	printNow("idle", false);
	turn.monitoring = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
	turn.step = Step::WaitOnline;
	return true;
}

bool DebugAsk::poll(){
	switch(turn.step){
		case Step::Idle:
			return false;

		case Step::WaitOnline:
			if(stopRequested()){
				printf("BBMEASURE turn error=aborted waited_ms=%u\n", (unsigned) msSince(turn.tStart));
				finish();
			}else if(ServiceLocator::WiFiServiceInstance->isOnline()){
				prepareAndSubmit();
			}else if(msSince(turn.tStart) >= OnlineTimeoutMs){
				printf("BBMEASURE turn error=offline waited_ms=%u\n", (unsigned) msSince(turn.tStart));
				finish();
			}
			break;

		case Step::Asking:
			if(ServiceLocator::AskWorkerInstance->busy()) break;
			reportAsk(*ServiceLocator::AskWorkerInstance);
			finish();
			break;
	}
	return turn.step != Step::Idle;
}

bool DebugAsk::running(){
	return turn.step != Step::Idle;
}

void DebugAsk::abort(){
	AskClient::abortInFlight();
}

void DebugAsk::stop(uint32_t warnEveryMs){
	if(turn.step == Step::Idle) return;
	abort();
	// The worker reads the capture buffer until it is idle: wait it out before ending the turn (C7).
	if(turn.step == Step::Asking){
		while(!ServiceLocator::AskWorkerInstance->waitIdle(warnEveryMs)){
			CMF_LOG(DebugAsk, LogLevel::Warning, "ask worker still busy, waiting another %u ms", (unsigned) warnEveryMs);
		}
	}
	finish();
}

#endif
