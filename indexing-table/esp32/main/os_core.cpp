/*
 * os_core.cpp
 * Copyright (C) 2023 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include <os_core.h>

OSCore* OSCore::instance = nullptr;

OSCore::OSCore()
{
}

void OSCore::init()
{

	if (instance != nullptr)
		return;
	instance = new OSCore();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
}

void OSCore::loop()
{
	ESP_LOGI(TAG, "Main | loop | loop started");
}

void OSCore::setup()
{
	// ------------------------------------------
	// Peripherals
	// ------------------------------------------
	ESP_LOGI(TAG, "setup | Peripherals");
	stepperControl.init();
	commEndpoint.setupComm();

	// on first step there is some kind of initialization taking place making it much slower
	steppers.stepStepper(stepperHalH, 3000, 15,true);
	steppers.stepStepper(stepperHalT, 3000, 15,true);

	// steppers.stepStepper(varsHalT, 30000, 10,true); // one rotation -> 6 seconds
	// vTaskDelay(6000 / portTICK_PERIOD_MS);
	// stepperControl.parseGCode("M80", 3);



	// ------------------------------------------
	// Configure Peripherals
	// ------------------------------------------
	// lcd->getLCD()->setCursor(0, 0);
	// lcd->getLCD()->print("Indexing Table Initialized");

	// ------------------------------------------
	// OSCora Tasker calls setup
	// ------------------------------------------

	// END
	ESP_LOGI(TAG, "setup | setup finished");
}


