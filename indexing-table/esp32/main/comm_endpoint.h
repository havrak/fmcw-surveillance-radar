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


class CommRequest{
	public:

		char command[MAX_COMMAND_LENGTH];
		bool error;

		CommRequest(const char* command, uint32_t command_len)
	{
		memset(this->command, 0, MAX_COMMAND_LENGTH);
		strncpy(this->command, command, command_len);
	};



};


class CommEndpoint {
	private:

		static CommEndpoint* instance;

		std::queue<CommRequest*> requestsQueue;

		constexpr static char TAG[] = "CommEndpoint";
inline static QueueHandle_t uart0_queue = NULL; // from recent FreeRTOS version QueueHandle_t doens't work if declared as static and inside of a class

		char responseBuffer[CONFIG_COMM_RS232_BUFFER_SIZE];

#ifdef CONFIG_WFM_ENABLE_LOCKS
    SemaphoreHandle_t lock;
#endif


		static void uartEvent(void* pvParameters);



	public:

		bool setupComm();

		CommEndpoint();




};

extern CommEndpoint commEndpoint;

#endif /* !COMM_ENDPOINT_H */
