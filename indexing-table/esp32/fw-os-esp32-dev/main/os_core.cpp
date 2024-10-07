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
	do {
		TaskerSingletonWrapper::getInstance()->tick();
		vTaskDelay(1);
	} while (true);
}

void OSCore::setup()
{
	Wire.begin(SDA, SCL);

	// ------------------------------------------
	// WiFi
	// ------------------------------------------
	// ESP_LOGI(TAG, "setup | WiFi");
	// WiFiManager::getInstance()->setOnConnectionEstablished(std::bind(&OSCore::connectionToAPEstablished, this));
	// WiFiManager::getInstance()->setOnConnectionLost(std::bind(&OSCore::connectionToAPLost, this));
	// WiFiManager::getInstance()->setRegime(WiFiRegime::WIFI_REGIME_STATION);

	// ------------------------------------------
	// Time Manager
	// ------------------------------------------

	// ------------------------------------------
	// Peripherals
	// ------------------------------------------
	ESP_LOGI(TAG, "setup | Peripherals");
	lcd = new PerI2CLCDDecorator(I2CPeriphery(0x27), 20, 4);
	PeripheralsManager::getInstance()->addPeriphery(lcd);

	PerStepperDriver* horizontal = nullptr;
	PerStepperDriver* tilt = nullptr;

	horizontal = new PerStepperDriver(CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_STEP_PIN1, CONFIG_MOTR_H_STEP_PIN2, CONFIG_MOTR_H_STEP_PIN3, CONFIG_MOTR_H_STEP_PIN4);

	tilt = new PerStepperDriver(CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_STEP_PIN1, CONFIG_MOTR_T_STEP_PIN2, CONFIG_MOTR_T_STEP_PIN3, CONFIG_MOTR_T_STEP_PIN4);

	MotorControl::getInstance()->setMotors(horizontal, tilt);
	PeripheralsManager::getInstance()->initializePeripherals();


	// ------------------------------------------
	// Configure Peripherals
	// ------------------------------------------
	lcd->getLCD()->setCursor(0, 0);
	lcd->getLCD()->print("Testo");

	// ------------------------------------------
	// Configure Peripherals
	// ------------------------------------------

	// ------------------------------------------
	// OSCora Tasker calls setup
	// ------------------------------------------
	TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_OS_CORE_TEST, 10'000, 0, TaskPriority::TSK_PRIORITY_MEDIUM));

	// END
	ESP_LOGI(TAG, "setup | setup finished");
}

uint8_t OSCore::call(uint16_t id)
{

	TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_OS_CORE_TEST, 10'000, 0, TaskPriority::TSK_PRIORITY_MEDIUM));
	return 0;
}

void OSCore::onTaskerEvent(CallbackInterface* eventSourceOwner, uint16_t eventSourceId, uint8_t code)
{
	return;
}
