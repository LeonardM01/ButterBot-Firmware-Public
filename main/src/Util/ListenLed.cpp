#include "ListenLed.h"
#include "ServiceLocator.h"

void setListenLed(bool on){
	LEDControl* led = ServiceLocator::LEDControlInstance.get();
	if(led == nullptr) return;
	if(on){
		led->startListen();
	}else{
		led->stopListen();
	}
}
