#include <unity.h>
#include <cstdlib>

void setUp(){}
void tearDown(){}

void runBackoffTests();

extern "C" void app_main(void){
	UNITY_BEGIN();
	runBackoffTests();
	// Exit explicitly: the FreeRTOS POSIX scheduler would otherwise keep the binary running.
	exit(UNITY_END() == 0 ? 0 : 1);
}
