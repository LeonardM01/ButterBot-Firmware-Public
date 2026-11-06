#include "AskWorker.h"
#include <esp_heap_caps.h>
#include <Log/Log.h>
#include "Services/WiFi/WiFiService.h"
#include "Util/Elapsed.h"
#include "Util/VoiceTurnHeap.h"

DEFINE_LOG(AskWorker)

namespace {
constexpr uint32_t IdlePollMs = 10;
}

AskWorker::AskWorker(Audio& audio, const WiFiService* wifi)
		: client(audio, generator, [this](AskPhase phase){ onPhase(phase); }), wifi(wifi){
	// TCB and stack in PSRAM (M3, see the class comment). With CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y a failed
	// allocation reboots rather than returning here (M11); at boot ~8 MB of PSRAM is free.
	if(xTaskCreatePinnedToCoreWithCaps(&AskWorker::taskMain, "AskWorker", StackBytes, this, Priority, &task, Core,
									   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS){
		task = nullptr;
		CMF_LOG(AskWorker, LogLevel::Error, "no PSRAM for the ask worker task");
	}
}

bool AskWorker::submit(uint32_t turnId, const uint8_t* wavBytes, size_t wavLen){
	if(task == nullptr || wavBytes == nullptr || wavLen == 0) return false;
	bool expected = false;
	if(!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;

	turn = turnId;
	wav = wavBytes;
	len = wavLen;
	xTaskNotifyGive(task); // publishes the job fields to the worker
	return true;
}

bool AskWorker::waitIdle(uint32_t timeoutMs) const{
	const int64_t tStart = nowUs();
	while(busy()){
		if(msSince(tStart) >= timeoutMs) return false;
		vTaskDelay(pdMS_TO_TICKS(IdlePollMs));
	}
	return true;
}

void AskWorker::taskMain(void* arg){
	static_cast<AskWorker*>(arg)->loop();
}

void AskWorker::loop(){
	bool onlineSeen = false;
	for(;;){
		// Until home Wi-Fi first comes up, wake once a second to log the idle heap the voice turn starts from.
		const TickType_t wait = onlineSeen ? portMAX_DELAY : pdMS_TO_TICKS(OnlinePollMs);
		const bool job = ulTaskNotifyTake(pdTRUE, wait) > 0;
		if(!onlineSeen && wifi != nullptr && wifi->isOnline()){
			onlineSeen = true;
			VoiceTurnHeap::logOnline();
		}
		if(!job) continue;

		heap = PhaseHeap{};
		last = client.ask(turn, wav, len);
		wav = nullptr;
		len = 0;
		stackFree = (uint32_t) uxTaskGetStackHighWaterMark(nullptr);
		CMF_LOG(AskWorker, LogLevel::Info, "stack min_free=%u of %u B", (unsigned) stackFree.load(), (unsigned) StackBytes);
		running.store(false, std::memory_order_release);
	}
}

void AskWorker::onPhase(AskPhase phase){
	switch(phase){
		case AskPhase::Connected:
			heap.connected = takeHeapSnapshot();
			break;
		case AskPhase::Playing:
			// Queued only: a filler may still be speaking. The voice turn's LED follows replyAudible() instead.
			heap.playing = takeHeapSnapshot();
			break;
		case AskPhase::Downloaded:
			heap.downloaded = takeHeapSnapshot();
			break;
	}
}
