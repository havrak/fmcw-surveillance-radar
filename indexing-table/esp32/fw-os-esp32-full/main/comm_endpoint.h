/*
 * comm_endpoint.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef COMM_ENDPOINT_H
#define COMM_ENDPOINT_H


#include <esp_log.h>
#include <queue>
#include <callback_interface.h>
#include <tasker_singleton_wrapper.h>
#include <os_core_tasker_ids.h>
#include <stepper_control.h>

#define EX_UART_NUM UART_NUM_0
#define MAX_COMMAND_LENGTH 64

#define LENGTH_TO_COPY(x, y) (x+y >= CONFIG_COMM_RS232_BUFFER_SIZE ? CONFIG_COMM_RS232_BUFFER_SIZE - x : y)


#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"


enum CommDataFormat : uint8_t {
	GCD = 0, // gcode
	ATC = 1, // AT command
};

class CommRequest{
	public:
		CommDataFormat format;

		char command[MAX_COMMAND_LENGTH]; // AT, gcode command
		bool error;

		CommRequest(CommDataFormat format, const char* command, uint32_t command_len): format(format)
	{
		memset(this->command, 0, MAX_COMMAND_LENGTH);
		strncpy(this->command, command, command_len);
	};



};


class CommEndpoint: CallbackInterface{
	private:

		static CommEndpoint* instance;

		std::queue<CommRequest*> requestsQueue;

		constexpr static char TAG[] = "CommEndpoint";
inline static QueueHandle_t uart0_queue = NULL; // from recent FreeRTOS version QueueHandle_t doens't work if declared as static and inside of a class

		char responseBuffer[CONFIG_COMM_RS232_BUFFER_SIZE];

		CommEndpoint();

#ifdef CONFIG_WFM_ENABLE_LOCKS
    SemaphoreHandle_t lock;
#endif


		bool setupComm();

		static void uartEvent(void* pvParameters);

		void processRequestGCD(const CommRequest* request);
		void processRequestATC(const CommRequest* request);

		void sendResponse(const CommRequest* request, const char* response, uint16_t responseLength);



	public:






		/**
		 * returns instance of CommEndpoint, instance needs to be created with createInstance call
		 *
		 * @return CommEndpoint*, nullptr if instance wasn't created
		 */
		static CommEndpoint* getInstance(){
			if(instance == nullptr)
			{
				instance = new CommEndpoint();
				ESP_LOGE(TAG, "getInstance | instance not created");
			}
			return instance;
		};

		/**
		 * callback from CallbackInterface, should NOT be called manually
		 */
		uint8_t call(uint16_t id) override;

		/**
		 * schedules processing of GCD request, data are copied and GCD is parsed
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestGCD(const char* input, uint16_t inputLength);

		/**
		 * schedules processing of ATC request, data are copied
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestATC(const char* input, uint16_t inputLength);

};

#endif /* !COMM_ENDPOINT_H */
