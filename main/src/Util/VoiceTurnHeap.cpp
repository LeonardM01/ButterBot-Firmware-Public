#include "VoiceTurnHeap.h"
#include <atomic>
#include <esp_heap_caps.h>
#include <Log/Log.h>

DEFINE_LOG(VoiceTurn)

namespace {

// State machine task only (C7): turns run there.
uint32_t turnNumber = 0;
bool turnRunning = false;

std::atomic<bool> onlineLogged{false};

const char* name(VoiceTurnHeap::Point point){
	switch(point){
		case VoiceTurnHeap::Point::Idle:
			return "idle";
		case VoiceTurnHeap::Point::Loaded:
			return "loaded";
		case VoiceTurnHeap::Point::Network:
			return "network";
		case VoiceTurnHeap::Point::AfterTurn:
			return "after";
	}
	return "?";
}

void startLowTracking(){
	// Restart cleanly if an earlier turn (or the measurement build) left the monitor running.
	if(heap_caps_monitor_local_minimum_free_size_start() != ESP_OK){
		(void) heap_caps_monitor_local_minimum_free_size_stop();
		(void) heap_caps_monitor_local_minimum_free_size_start();
	}
}

}

HeapSnapshot VoiceTurnHeap::beginTurn(){
	if(turnRunning){
		endTurn();
	}
	turnRunning = true;
	turnNumber++;
	startLowTracking();
	const HeapSnapshot idle = takeHeapSnapshot();
	log(Point::Idle, idle);
	return idle;
}

bool VoiceTurnHeap::modelLoadAllowed(const HeapSnapshot& idle){
	return idle.intFree >= ModelLoadMinIntFree && idle.intLargest >= ModelLoadMinIntLargest;
}

void VoiceTurnHeap::log(Point point, const HeapSnapshot& s){
	if(!s.taken){
		CMF_LOG(VoiceTurn, LogLevel::Info, "heap turn=%u at=%s not_reached", (unsigned) turnNumber, name(point));
		return;
	}
	CMF_LOG(VoiceTurn, LogLevel::Info, "heap turn=%u at=%s int_free=%u int_min=%u int_largest=%u psram_free=%u psram_largest=%u",
			(unsigned) turnNumber, name(point), (unsigned) s.intFree, (unsigned) s.intMin, (unsigned) s.intLargest,
			(unsigned) s.psFree, (unsigned) s.psLargest);
}

void VoiceTurnHeap::mark(Point point){
	log(point, takeHeapSnapshot());
}

void VoiceTurnHeap::endTurn(){
	if(!turnRunning) return;
	mark(Point::AfterTurn);
	(void) heap_caps_monitor_local_minimum_free_size_stop();
	turnRunning = false;
}

void VoiceTurnHeap::logOnline(){
	if(onlineLogged.exchange(true)) return;
	const HeapSnapshot s = takeHeapSnapshot();
	CMF_LOG(VoiceTurn, LogLevel::Info, "heap at=online int_free=%u int_min=%u int_largest=%u psram_free=%u psram_largest=%u "
			"guard_free=%u guard_largest=%u",
			(unsigned) s.intFree, (unsigned) s.intMin, (unsigned) s.intLargest, (unsigned) s.psFree, (unsigned) s.psLargest,
			(unsigned) ModelLoadMinIntFree, (unsigned) ModelLoadMinIntLargest);
}
