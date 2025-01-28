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
#include <comm_endpoint.h>
#include <nvs_flash.h>
#include <stepper_control.h>
#include "esp_task_wdt.h"


#include <stepper_hal.h>
#include <functional>


class OSCore {
	private:


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


};

#endif /* !OS_CORE_H */
