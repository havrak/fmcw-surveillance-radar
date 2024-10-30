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
#include <wifi_manager.h>
#include <modbus_wrapper.h>
#include <nvs_flash.h>

#include <peripherals_manager.h>
#include <os_core_tasker_ids.h>
#include <functional>
#include <time_manager.h>

#include <callback_interface.h>
#include <modbus_slave_event_observer.h>
#include <modbus_wrapper.h>

#define INPUT_REG_SIZE 128
#define HOLDING_REG_SIZE 128


class OSCore : CallbackInterface, TaskerEventObserver, ModbusSlaveEventObserver{
	private:
	uint16_t* discrete_reg = nullptr;
	uint16_t* coil_reg = nullptr;
	uint16_t input_reg[INPUT_REG_SIZE] = { 0 };
	uint16_t holding_reg[HOLDING_REG_SIZE] = { 0 };


	constexpr static char TAG[] = "OSCore";
	static OSCore* instance;

	OSCore();


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

	void onModbusSlaveEvent(mb_event_group_t eventGroup, uint8_t offset, uint8_t size, void* ptr) override;

	void temperatureDataRead(){

	}

};

#endif /* !OS_CORE_H */
