#define TWDT_TIMEOUT_MS  4000

// #include <os_core.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <comm_endpoint.h>
#include <nvs_flash.h>
#include <stepper_control.h>
#include "esp_task_wdt.h"

extern "C" void app_main(void)
{
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	commEndpoint.setupComm();
	stepperControl.init();
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	stepperControl.parseGCode("G92", 3);
	stepperControl.parseGCode("M92 Y200 P400", 13);




	// NOTE: DEMO CODE
	stepperControl.parseGCode("P90 xx_ro", 14);
	stepperControl.parseGCode("G91", 3);
	stepperControl.parseGCode("G21",3);
	stepperControl.parseGCode("G92",3);
	stepperControl.parseGCode("P29",3); // never stop
	stepperControl.parseGCode("M03 SY5 Y+",10);
	stepperControl.parseGCode("P91", 3);
	stepperControl.parseGCode("P92", 3);

	stepperControl.parseGCode("P90 xx_ti", 14);
	stepperControl.parseGCode("G91", 3);
	stepperControl.parseGCode("G21",3);
	stepperControl.parseGCode("G92",3);
	stepperControl.parseGCode("P29",3);
	stepperControl.parseGCode("M03 SY5 Y+",10);
	stepperControl.parseGCode("P91", 3);
	stepperControl.parseGCode("G0 SP15 P60",11);
	stepperControl.parseGCode("G0 SP15 P-60",12);
	stepperControl.parseGCode("P92", 3);
}
