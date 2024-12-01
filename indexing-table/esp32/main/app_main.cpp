#define TWDT_TIMEOUT_MS  4000

#include <os_core.h>

extern "C" void app_main(void)
{
	OSCore::init();
	OSCore::getInstance()->setup();
	OSCore::getInstance()->loop();
}

