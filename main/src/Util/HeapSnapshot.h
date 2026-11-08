#ifndef BUTTERBOT_FIRMWARE_HEAPSNAPSHOT_H
#define BUTTERBOT_FIRMWARE_HEAPSNAPSHOT_H

#include <cstddef>
#include <esp_heap_caps.h>

/**
 * Internal RAM and PSRAM at one moment (M8: free size and largest block). `intMin`/`psMin` are
 * heap_caps_get_minimum_free_size(), i.e. the low since boot, or since the running
 * heap_caps_monitor_local_minimum_free_size_start() call.
 */
struct HeapSnapshot {
	bool taken = false;
	size_t intFree = 0, intMin = 0, intLargest = 0;
	size_t psFree = 0, psMin = 0, psLargest = 0;
};

inline HeapSnapshot takeHeapSnapshot(){
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

#endif //BUTTERBOT_FIRMWARE_HEAPSNAPSHOT_H
