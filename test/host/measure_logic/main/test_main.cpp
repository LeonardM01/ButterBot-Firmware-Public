#include <unity.h>
#include <cstdio>
#include <unistd.h>

void setUp(){}

void tearDown(){}

void runPowerPressTests();

extern "C" void app_main(void){
	UNITY_BEGIN();
	runPowerPressTests();
	const int failures = UNITY_END();
	// The FreeRTOS POSIX port keeps the scheduler alive, so leave explicitly with Unity's verdict.
	// _exit skips atexit handlers, which can deadlock against the port's threads.
	fflush(stdout);
	_exit(failures == 0 ? 0 : 1);
}
