#include "PowerPress.h"

PowerPress classifyPowerPress(uint64_t heldMs){
	if(heldMs <= ShortPressMaxMs) return PowerPress::Short;
	if(heldMs >= MediumPressMinMs && heldMs <= MediumPressMaxMs) return PowerPress::Medium;
	return PowerPress::Ignored;
}
