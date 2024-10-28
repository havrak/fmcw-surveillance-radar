/*
 * os_core.h
 * Copyright (C) 2023 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef OS_CORE_H
#define OS_CORE_H

#include <esp_event.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <tasker_event_observer.h>
#include <per_i2c_lcd_decorator.h>
#include <i2c_periphery.h>
#include <wifi_manager.h>
#include <nvs_flash.h>
#include <motor_control.h>
#include <per_stepper_driver.h>
#include "esp_task_wdt.h"



#include <peripherals_manager.h>
#include <os_core_tasker_ids.h>
#include <functional>

#include <callback_interface.h>


class OSCore : CallbackInterface, TaskerEventObserver{
	private:


	constexpr static char TAG[] = "OSCore";
	static OSCore* instance;

	OSCore();
	PerI2CLCDDecorator* lcd = nullptr;


	public:

	static OSCore* getInstance()
	{
		return instance;
	}

	static void init();
	void setup();
	void loop();

	uint8_t call(uint16_t id) override;

	void onTaskerEvent(CallbackInterface* eventSourceOwner, uint16_t eventSourceId, uint8_t code) override;

	void connectionToAPEstablished(){
		ESP_LOGI(TAG, "connectionToAPEstablished | established connection");
	}

	void connectionToAPLost(){
		ESP_LOGI(TAG, "connectionToAPLost | lost conenction");
	}

};

#endif /* !OS_CORE_H */
