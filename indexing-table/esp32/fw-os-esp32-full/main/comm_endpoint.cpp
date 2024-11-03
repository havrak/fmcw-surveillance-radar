/*
 * comm_endpoint.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "comm_endpoint.h"

CommEndpoint* CommEndpoint::instance = nullptr;

CommEndpoint::CommEndpoint()
{
#ifdef CONFIG_COMM_ENABLE_LOCKS
	lock = xSemaphoreCreateMutex();
	if (lock == NULL) {
		ESP_LOGE(TAG, "CommEndpoint | xSemaphoreCreateMutex failed");
	} else {
		ESP_LOGI(TAG, "CommEndpoint | created lock");
	}
#endif
	setupComm();
}


void CommEndpoint::uartEvent(void* commEndpointInstance) // TODO pin to core 0
{
	auto startIndex = [](const char* str) { // skips first 4 characters and all spaces and tabs
		int i = 4;
		while (str[i] == ' ' || str[i] == '\t')
			i++;
		return i;
	};

	static uart_event_t event;
	static uint8_t* dtmp = (uint8_t*)malloc(CONFIG_COMM_RS232_BUFFER_SIZE);

	static char buffer[CONFIG_COMM_RS232_BUFFER_SIZE + 2];
	static int i = 0;
	static int j = 0;
	static int j_prev = 0;
	static char* pointer = 0;
	i = 0;
	j = 0;
	j_prev = 0;
	pointer = 0;
	while (1) {
		if (xQueueReceive(uart0_queue, (void*)&event, (TickType_t)portMAX_DELAY)) {
			memset(dtmp, 0, sizeof(CONFIG_COMM_RS232_BUFFER_SIZE));
			if (event.type == UART_DATA) {
				uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);

				for (j = 0, j_prev = 0; j < event.size; j++) {
					if ((int)dtmp[j] == 13) {   // match carriage return
						memcpy(buffer + i, dtmp + j_prev, LENGTH_TO_COPY(i, j - j_prev));
						i += LENGTH_TO_COPY(i, j - j_prev);
						buffer[i] = '\0';


						pointer = buffer + startIndex(buffer);
						if (strncmp(buffer, "#ATC", 4) == 0)
							((CommEndpoint*) commEndpointInstance)->logRequestATC(pointer, strlen(pointer));
						if (strncmp(buffer, "#GCD", 4) == 0)
							((CommEndpoint*) commEndpointInstance)->logRequestGCD(pointer, strlen(pointer));
						i = 0;
						j_prev = j + 1;
					}
				}
				memcpy(buffer + i, dtmp + j_prev, LENGTH_TO_COPY(i, event.size - j_prev)); // copy rest of data
				i += LENGTH_TO_COPY(i, event.size - j_prev);
			}
		}
	}
	free(dtmp);
	dtmp = NULL;
	vTaskDelete(NULL);
}

bool CommEndpoint::setupComm()
{
	ESP_LOGI(TAG, "setupComm | Setting up serial");
	esp_err_t err;
	uart_config_t uart_config = {
		.baud_rate = CONFIG_COMM_RS232_BAUDRATE,
		// .bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	// Install UART driver, and get the queue.
	uart_driver_install(EX_UART_NUM, CONFIG_COMM_RS232_BUFFER_SIZE * 2, CONFIG_COMM_RS232_BUFFER_SIZE * 2, 20, &uart0_queue, 0);

	err=uart_param_config(EX_UART_NUM, &uart_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "setupComm | uart_param_config failed");
		uart_driver_delete(EX_UART_NUM);
		return false;
	}
	err=uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "setupComm | uart_set_pin failed");
		uart_driver_delete(EX_UART_NUM);
		return false;
	}

	// Create a task to handler UART event from ISR
	xTaskCreatePinnedToCore(uartEvent, "uartEvent", 2048, this, 12, NULL, 0);
	return true;
}

bool CommEndpoint::logRequestATC(const char* input, uint16_t inputLength)
{
#ifdef CONFIG_COMM_ENABLE_LOCKS
	if (xSemaphoreTake(lock, (TickType_t)1000) != pdTRUE)
		return false;
#endif

	requestsQueue.push(new CommRequest(CommDataFormat::ATC, input, inputLength));
	if (requestsQueue.size() == 1)
		return TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_PROCESS_COMM_REQUEST, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));

#ifdef CONFIG_COMM_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif
	return true;
}

bool CommEndpoint::logRequestGCD(const char* input, uint16_t inputLength)
{
#ifdef CONFIG_COMM_ENABLE_LOCKS
	if (xSemaphoreTake(lock, (TickType_t)1000) != pdTRUE)
		return false;
#endif
	static StepperControl* motorControl = StepperControl::getInstance();

	// NOTE: gcode command are processed immediately (usually means move commands are just scheduled and executed in rtos task)
	CommRequest* request = new CommRequest(CommDataFormat::GCD, input, inputLength);
	request->error =  motorControl->parseGcode(input, inputLength);
	requestsQueue.push(request);

	if (requestsQueue.size() == 1) {
		return TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_PROCESS_COMM_REQUEST, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));
	}
#ifdef CONFIG_COMM_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif

	return true;
}

void CommEndpoint::sendResponse(const CommRequest* request, const char* response, uint16_t responseLength)
{
	switch (request->format) {
		case CommDataFormat::ATC:
			uart_write_bytes(UART_NUM_0, "#ATC\n", 5);
			break;
		case CommDataFormat::GCD:
			uart_write_bytes(UART_NUM_0, "#GCD\n", 5);
			break;
	}
	uart_write_bytes(UART_NUM_0, response, responseLength);
}

void CommEndpoint::processRequestGCD(const CommRequest* request)
{
	static const char* strOK = "[0K]\n";
	static const char* strERR = "[ERR]\n";
	static char responseBuffer[CONFIG_COMM_RS232_BUFFER_SIZE+6];
	memcpy(responseBuffer, request->command, strlen(request->command));
	memcpy(responseBuffer + strlen(request->command)+1, request->error ? strERR : strOK, request->error ? 5 : 4);
	sendResponse(request, responseBuffer, strlen(request->command) + (request->error ? 6 : 5));
}

void CommEndpoint::processRequestATC(const CommRequest* request)
{
	static const char* strOK = "0K\n";
	static const char* strERR = "ERR\n";

	auto routineGetInfo = [request, this]() { // return device info
		uint64_t uptime = esp_timer_get_time() / 1000;
		memset(responseBuffer, 0, 64);
		sprintf(responseBuffer, "OK\n%llu\n", uptime);
		sendResponse(request, responseBuffer, strlen(responseBuffer));
		// -> call to os
	};

	auto routineGetFirmwareVersion = [request, this]() { // return firmware version
		ESP_LOGE(TAG, "processRequestAT | AT+GMR, Not implemented");
		// call to os
	};


	auto routineReset = [request, this]() { // reset device, reboots
		sendResponse(request, strOK, 4);
		vTaskDelay(1);
		esp_restart();
	};



	// Determine which command was sent
	if (strncmp(request->command, "AT+INF", 6) == 0) // return device info
		routineGetInfo();
	else if (strncmp(request->command, "AT+GMR", 6) == 0) // return firmware version
		routineGetFirmwareVersion();
	else if (strncmp(request->command, "AT+RST", 6) == 0) // reset device, reboots
		routineReset();
	else { // unknown command
		sendResponse(request, strERR, 5);
		ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", request->command);
	}
}


uint8_t CommEndpoint::call(uint16_t id)
{
	if (requestsQueue.size() == 0)
		return 0;
#ifdef CONFIG_COMM_ENABLE_LOCKS
	xSemaphoreTake(lock, portMAX_DELAY);
#endif

	CommRequest* request = requestsQueue.front();
	requestsQueue.pop();
	if (requestsQueue.size() != 0) // schedule before processing as to prevent it being scheduled twice in handler of some event
		TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_PROCESS_COMM_REQUEST, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));

#ifdef CONFIG_COMM_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif

	switch (request->format) {
		case CommDataFormat::GCD:
			processRequestGCD(request);
			break;
		case CommDataFormat::ATC:
			processRequestATC(request);
			break;
	}
	delete request;

	return 0;
}
