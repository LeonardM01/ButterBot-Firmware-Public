#ifndef BUTTERBOT_FIRMWARE_ELAPSED_H
#define BUTTERBOT_FIRMWARE_ELAPSED_H

#include <cstdint>
#include <esp_timer.h>

/** Microseconds since boot (esp_timer), for timing phases of a turn. */
inline int64_t nowUs(){
	return esp_timer_get_time();
}

/** Whole milliseconds from one nowUs() timestamp to a later one. */
inline uint32_t msBetween(int64_t fromUs, int64_t toUs){
	return (uint32_t) ((toUs - fromUs) / 1000);
}

/** Whole milliseconds since a nowUs() timestamp. */
inline uint32_t msSince(int64_t startUs){
	return msBetween(startUs, nowUs());
}

#endif //BUTTERBOT_FIRMWARE_ELAPSED_H
