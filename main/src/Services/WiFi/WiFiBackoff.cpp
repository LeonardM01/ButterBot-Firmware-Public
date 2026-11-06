#include "WiFiBackoff.h"

uint32_t WiFiBackoff::delayMs(uint32_t attempt) noexcept{
	uint32_t delay = InitialDelayMs;

	// Double step by step so a large attempt count can never overflow the shift.
	for(uint32_t i = 0; i < attempt && delay < MaxDelayMs; ++i){
		delay *= 2;
	}

	return delay < MaxDelayMs ? delay : MaxDelayMs;
}
