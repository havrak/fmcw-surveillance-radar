/*
 * serial_interface.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "jopka_endpoint.h"

QueueHandle_t JoPkaEndpoint::uart0_queue;
JoPkaEndpoint* JoPkaEndpoint::instance = nullptr;
MQTTWrapper* JoPkaEndpoint::mqttWrapperPtr = nullptr;

char* JoPkaDeviceModeToString(JoPkaDeviceMode mode)
{
	switch (mode) {
	case JoPkaDeviceMode::MODE_SERVICE:
		return "MODE_SERVICE";
	case JoPkaDeviceMode::MODE_FLASH:
		return "MODE_FLASH";
	case JoPkaDeviceMode::MODE_RUN:
		return "MODE_RUN";
	case JoPkaDeviceMode::MODE_STORAGE:
		return "MODE_STORAGE";
	default:
		return "UNKNOWN";
	}
}

JoPkaEndpoint::JoPkaEndpoint(MQTTWrapper* mqttWrapperPtr, bool serial)
{
#ifdef CONFIG_JPK_ENABLE_LOCKS
	lock = xSemaphoreCreateMutex();
	if (lock == NULL) {
		ESP_LOGE(TAG, "JoPkaEndpoint | xSemaphoreCreateMutex failed");
	} else {
		ESP_LOGI(TAG, "JoPkaEndpoint | created lock");
	}
#endif

	this->mqttWrapperPtr = mqttWrapperPtr;
	if (serial)
		setupSerial();
}

JoPkaEndpoint::JoPkaEndpoint(bool serial)
{
#ifdef CONFIG_JPK_ENABLE_LOCKS
	lock = xSemaphoreCreateMutex();
	if (lock == NULL) {
		ESP_LOGE(TAG, "JoPkaEndpoint | xSemaphoreCreateMutex failed");
	} else {
		ESP_LOGI(TAG, "JoPkaEndpoint | created lock");
	}
#endif
	mqttWrapperPtr = MQTTSingletonWrapper::getInstance();

	if (serial)
		setupSerial();
}

JoPkaEndpoint::JoPkaEndpoint(bool mqtt, bool serial)
{
	if (mqtt) {
		mqttWrapperPtr = new MQTTWrapper(CONFIG_JPK_MQTT_BROKER_URL, CONFIG_JPK_MQTT_BROKER_USERNAME, CONFIG_JPK_MQTT_BROKER_PASSWORD);
	}

	if (serial)
		setupSerial();
}

void JoPkaEndpoint::uartEvent(void* pvParameters)
{
	auto startIndex = [](const char* str) { // skips first 4 characters and all spaces and tabs
		int i = 4;
		while (str[i] == ' ' || str[i] == '\t')
			i++;
		return i;
	};

	static uart_event_t event;
	static uint8_t* dtmp = (uint8_t*)malloc(CONFIG_JPK_RS232_BUFFER_SIZE);

	static char buffer[CONFIG_JPK_RS232_BUFFER_SIZE + 2];
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
			memset(dtmp, 0, sizeof(CONFIG_JPK_RS232_BUFFER_SIZE));
			if (event.type == UART_DATA) {
				uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);

				for (j = 0, j_prev = 0; j < event.size; j++) {
					if ((int)dtmp[j] == 13) {
						memcpy(buffer + i, dtmp + j_prev, LENGTH_TO_COPY(i, j - j_prev));
						i += LENGTH_TO_COPY(i, j - j_prev);
						buffer[i] = '\0';

						pointer = buffer + startIndex(buffer);
						if (strncmp(buffer, "#ATC", 4) == 0)
							JoPkaEndpoint::getInstance()->logRequestATC(pointer, strlen(pointer), JoPkaDataOrigin::RS232);
						if (strncmp(buffer, "#JSN", 4) == 0)
							JoPkaEndpoint::getInstance()->logRequestJSN(pointer, strlen(pointer), JoPkaDataOrigin::RS232);
						if (strncmp(buffer, "#LRW", 4) == 0)
							JoPkaEndpoint::getInstance()->logRequestLRW(pointer, strlen(pointer), JoPkaDataOrigin::RS232);
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

void JoPkaEndpoint::setupSerial()
{
	ESP_LOGI(TAG, "setupSerial | Setting up serial");
	uart_config_t uart_config = {
		.baud_rate = CONFIG_JPK_RS232_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	// Install UART driver, and get the queue.
	uart_driver_install(EX_UART_NUM, CONFIG_JPK_RS232_BUFFER_SIZE * 2, CONFIG_JPK_RS232_BUFFER_SIZE * 2, 20, &uart0_queue, 0);
	uart_param_config(EX_UART_NUM, &uart_config);
	uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	// Create a task to handler UART event from ISR
	xTaskCreate(uartEvent, "uartEvent", 2048, NULL, 12, NULL);
}

bool JoPkaEndpoint::toggleMQTT(bool toggle)
{
	if (toggle) {
		if (!mqttWrapperPtr->connect())
			return false;
		subscribeMQTTopics();
	} else {
		unsubscribeMQTTopics();
		if (!mqttWrapperPtr->disconnect())
			return false;
	}
	return true;
}
void JoPkaEndpoint::subscribeMQTTopics()
{
	mqttWrapperPtr->subscribe("/jopka/downlink/ATC");
	mqttWrapperPtr->addObserver(this, "/jopka/downlink/ATC");

	mqttWrapperPtr->subscribe("/jopka/downlink/JSN");
	mqttWrapperPtr->addObserver(this, "/jopka/downlink/JSN");
}

void JoPkaEndpoint::unsubscribeMQTTopics()
{
	mqttWrapperPtr->unsubscribe("/jopka/downlink/ATC");
	mqttWrapperPtr->removeObserver(this, "/jopka/downlink/ATC");

	mqttWrapperPtr->unsubscribe("/jopka/downlink/JSN");
	mqttWrapperPtr->removeObserver(this, "/jopka/downlink/JSN");
}

void JoPkaEndpoint::onMQTTConnected(){
	subscribeMQTTopics();
}

void JoPkaEndpoint::onMQTTEvent(const char* topic, ssize_t topic_len, const char* data, ssize_t data_len)
{
	if (strncmp(topic, "/jopka/downlink/JSN", topic_len <= 19 ? topic_len : 19) == 0) {
		ESP_LOGI(TAG, "Received downlink message JSN");
		logRequestJSN(data, data_len, JoPkaDataOrigin::MQTT);
	} else if (strncmp(topic, "/jopka/downlink/ATC", topic_len <= 19 ? topic_len : 19) == 0) {
		ESP_LOGI(TAG, "Received downlink message ATC");
		logRequestATC(data, data_len, JoPkaDataOrigin::MQTT);
	} else if (strncmp(topic, "/jopka/downlink/LRW", topic_len <= 19 ? topic_len : 19) == 0) {
		ESP_LOGI(TAG, "Received downlink message LRW");
		logRequestLRW(data, data_len, JoPkaDataOrigin::MQTT);
	}
}

bool JoPkaEndpoint::logRequestATC(const char* input, uint16_t inputLength, JoPkaDataOrigin origin)
{
#ifdef CONFIG_JPK_ENABLE_LOCKS
	if (xSemaphoreTake(lock, (TickType_t)1000) != pdTRUE)
		return false;
#endif

	requestsQueue.push(new JoPkaRequest(origin, JoPkaDataFormat::ATC, input, inputLength));
	if (requestsQueue.size() == 1)
		return TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_JOPKA_PROCESS, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));

#ifdef CONFIG_JPK_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif
	return true;
}

bool JoPkaEndpoint::logRequestLRW(const char* input, uint16_t inputLength, JoPkaDataOrigin origin)
{
#ifdef CONFIG_JPK_ENABLE_LOCKS
	if (xSemaphoreTake(lock, (TickType_t)1000) != pdTRUE)
		return false;
#endif
	requestsQueue.push(new JoPkaRequest(origin, JoPkaDataFormat::LRW, input, inputLength));
	if (requestsQueue.size() == 1)
		return TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_JOPKA_PROCESS, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));

#ifdef CONFIG_JPK_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif

	return true;
}

bool JoPkaEndpoint::logRequestJSN(const char* input, uint16_t inputLength, JoPkaDataOrigin origin)
{
#ifdef CONFIG_JPK_ENABLE_LOCKS
	if (xSemaphoreTake(lock, (TickType_t)1000) != pdTRUE)
		return false;
#endif

	cJSON* root = cJSON_ParseWithLength(input, inputLength); // WARNING: could be slow on big messages thus cause problems with serial
	cJSON* array = cJSON_GetObjectItem(root, "m");

	if (array == NULL) {
#ifdef CONFIG_JPK_ENABLE_LOCKS
		xSemaphoreGive(lock);
#endif

		return false;
	}
	for (int i = 0; i < cJSON_GetArraySize(array); i++)
		requestsQueue.push(new JoPkaRequest(origin, cJSON_GetArrayItem(array, i)));

	if (requestsQueue.size() == 1) {
		return TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_JOPKA_PROCESS, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));
	}
#ifdef CONFIG_JPK_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif

	return true;
}

void JoPkaEndpoint::sendResponse(const JoPkaRequest* request, const char* response, uint16_t responseLength)
{
	if (request->origin == JoPkaDataOrigin::RS232) {
		switch (request->format) {
		case JoPkaDataFormat::ATC:
			uart_write_bytes(UART_NUM_0, "#ATC\n", 5);
			break;
		case JoPkaDataFormat::LRW:
			uart_write_bytes(UART_NUM_0, "#LRW\n", 5);
			break;
		case JoPkaDataFormat::JSN:
			uart_write_bytes(UART_NUM_0, "#JSN\n", 5);
			break;
		}
		uart_write_bytes(UART_NUM_0, response, responseLength);
	} else if (request->origin == JoPkaDataOrigin::MQTT) {
		switch (request->format) {
		case JoPkaDataFormat::ATC:
			mqttWrapperPtr->publish("/jopka/uplink/ATC", response, responseLength);
			break;
		case JoPkaDataFormat::LRW:
			mqttWrapperPtr->publish("/jopka/uplink/LRW", response, responseLength);
			break;
		case JoPkaDataFormat::JSN:
			mqttWrapperPtr->publish("/jopka/uplink/JSN", response, responseLength);
			break;
		}
	}
}

void JoPkaEndpoint::processRequestJSN(const JoPkaRequest* request)
{

	cJSON* id = cJSON_GetObjectItem(request->data.jsn, "id");
	cJSON* command = cJSON_GetObjectItem(request->data.jsn, "c");
	cJSON* params = cJSON_GetObjectItem(request->data.jsn, "p");

	cJSON* response = cJSON_CreateObject();
	cJSON* array = cJSON_CreateArray();
	cJSON_AddItemToObject(response, "m", array);
	cJSON* responseMessage = cJSON_CreateObject();
	cJSON_AddItemToArray(array, responseMessage);
	cJSON* idCopy = cJSON_Duplicate(id, 1);
	cJSON_AddItemToObject(responseMessage, "id", idCopy);

	auto routineBasicGetInfo = [this, request](cJSON* responseMessage, cJSON* params) { // basic/GET_DEVICE_INFO
		cJSON* statusCode = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
		cJSON* deviceInfo = cJSON_CreateObject();
		// <- push in message info
		cJSON* uptime = cJSON_CreateNumber(esp_timer_get_time() / 1000);
		cJSON_AddItemToObject(deviceInfo, "u", uptime);
		cJSON* mode = cJSON_CreateString(JoPkaDeviceModeToString(JoPkaEndpoint::getInstance()->deviceMode));
		cJSON_AddItemToObject(deviceInfo, "m", mode);
		cJSON_AddItemToObject(responseMessage, "p", deviceInfo);

	};

	auto routineBasicFactoryReset = [this, request](cJSON* responseMessage, cJSON* params) { // basic/FACTORY_RESET
		deviceMode = JoPkaDeviceMode::MODE_SERVICE;
		if (jpkModeChangeRequestCallback != nullptr)
			jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_SERVICE);
	};

	auto routineDevModeGetMode = [this, request](cJSON* responseMessage, cJSON* params) { // DEVmode/gMODE
		cJSON* statusCode = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
		cJSON* deviceInfo = cJSON_CreateObject();
		cJSON* mode = cJSON_CreateString(JoPkaDeviceModeToString(JoPkaEndpoint::getInstance()->deviceMode));
		cJSON_AddItemToObject(deviceInfo, "M", mode);
		cJSON_AddItemToObject(responseMessage, "p", deviceInfo);
	};

	auto routineDevModeSetFlash = [this, request](cJSON* responseMessage, cJSON* params) { // DEVmode/sFLASH
		deviceMode = JoPkaDeviceMode::MODE_FLASH;
		if (jpkModeChangeRequestCallback != nullptr)
			jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_FLASH);
	};

	auto routineDevModeSetRun = [this, request](cJSON* responseMessage, cJSON* params) { // DEVmode/sRUN
		deviceMode = MODE_RUN;
		if (jpkModeChangeRequestCallback != nullptr)
			jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_RUN);
	};

	auto routineDevModeSetStorage = [this, request](cJSON* responseMessage, cJSON* params) { // DEVmode/sSTORAGE
		deviceMode = JoPkaDeviceMode::MODE_STORAGE;
		if (jpkModeChangeRequestCallback != nullptr)
			jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_STORAGE);
	};

	auto routineTimeGetOffset = [this, request](cJSON* responseMessage, cJSON* params) { // time/gO
#ifndef CONFIG_TME_TIMEZONE_ENABLE
		cJSON* statusCode = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
		cJSON* deviceInfo = cJSON_CreateObject();
		cJSON* offset = cJSON_CreateNumber(TimeManager::getInstance()->getOffset());
		cJSON_AddItemToObject(deviceInfo, "O", offset);
		cJSON_AddItemToObject(responseMessage, "p", deviceInfo);
#else
		cJSON* statusCode = cJSON_CreateNumber(-1);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
#endif
	};

	auto routineTimeSetOffset = [this, request](cJSON* responseMessage, cJSON* params) { // time/sO
#ifndef CONFIG_TME_TIMEZONE_ENABLE
		uint64_t time = cJSON_GetObjectItem(params, "O")->valueint;
		TimeManager::getInstance()->updateOffset(time);
		cJSON* statusCode = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
#else
		cJSON* statusCode = cJSON_CreateNumber(-1);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
#endif
	};

	auto routineTimeGetTime = [this, request](cJSON* responseMessage, cJSON* params) { // time/gT
		cJSON* statusCode = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
		cJSON* time = cJSON_CreateNumber(TimeManager::getInstance()->getTime());
		cJSON* deviceInfo = cJSON_CreateObject();
		cJSON_AddItemToObject(deviceInfo, "T", time);
		cJSON_AddItemToObject(responseMessage, "p", deviceInfo);
	};

	auto routineTimeSetTime = [this, request](cJSON* responseMessage, cJSON* params) { // time/sT
		uint64_t time = cJSON_GetObjectItem(params, "T")->valueint;
		TimeManager::getInstance()->updateTime(time);
		cJSON* statusCode = cJSON_CreateNumber(0);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
	};

	auto routinePowerGetBatteryLevel = [this, request](cJSON* responseMessage, cJSON* params) { // power/gBl
		cJSON* statusCode = cJSON_CreateNumber(-2);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
	};

	if (strcmp(command->valuestring, "basic/GET_DEVICE_INFO") == 0) // return device info
		routineBasicGetInfo(responseMessage, params);
	else if (strcmp(command->valuestring, "basic/FACTORY_RESET") == 0) // reset device to factory settings
		routineBasicFactoryReset(responseMessage, params);
	else if (strcmp(command->valuestring, "DEVmode/gMODE") == 0) // return current mode
		routineDevModeGetMode(responseMessage, params);
	else if (strcmp(command->valuestring, "DEVmode/sFLASH") == 0) // set to flash mode, expects HEX data containing firmware
		routineDevModeSetFlash(responseMessage, params);
	else if (strcmp(command->valuestring, "DEVmode/sRUN") == 0) // set to run mode
		routineDevModeSetRun(responseMessage, params);
	else if (strcmp(command->valuestring, "DEVmode/sSTORAGE") == 0) // set to storage mode
		routineDevModeSetStorage(responseMessage, params);
	else if (strcmp(command->valuestring, "time/gT") == 0) // return time (unix timestamp)
		routineTimeGetTime(responseMessage, params);
	else if (strcmp(command->valuestring, "time/sT") == 0) // set time (unix timestamp)
		routineTimeSetTime(responseMessage, params);
	else if (strcmp(command->valuestring, "time/gO") == 0) // return time (unix timestamp)
		routineTimeGetOffset(responseMessage, params);
	else if (strcmp(command->valuestring, "time/sO") == 0) // set time (unix timestamp)
		routineTimeSetOffset(responseMessage, params);
	else if (strcmp(command->valuestring, "power/gBl") == 0) // return battery level
		routinePowerGetBatteryLevel(responseMessage, params);
	else {
		ESP_LOGE(TAG, "processRequestJSN | Unknown command: %s", command->valuestring);
		cJSON* statusCode = cJSON_CreateNumber(-1);
		cJSON_AddItemToObject(responseMessage, "s", statusCode);
	}

	char* str = cJSON_PrintUnformatted(response);
	ESP_LOGI(TAG, "processRequestJSN | Sending response: %s", str);
	sendResponse(request, str, strlen(str));
	cJSON_Delete(response);
	delete str;
}

void JoPkaEndpoint::processRequestATC(const JoPkaRequest* request)
{
	static const char* strOK = "0K\n";
	static const char* strERR = "ERR\n";

	auto routineGetInfo = [request, this]() { // return device info
		uint64_t uptime = esp_timer_get_time() / 1000;
		memset(responseBuffer, 0, 64);
		char* mode = JoPkaDeviceModeToString(deviceMode);
		memset(responseBuffer, 0, 128);
		sprintf(responseBuffer, "OK\n%llu\n%s\n", uptime, mode);
		sendResponse(request, responseBuffer, strlen(responseBuffer));
		// -> call to os
	};

	auto routineGetFirmwareVersion = [request, this]() { // return firmware version
		ESP_LOGE(TAG, "processRequestAT | AT+GMR, Not implemented");
		// call to os
	};

	auto routineGetMac = [request, this]() { // return device mac
		uint8_t address[6];
		esp_read_mac(address, ESP_MAC_EFUSE_FACTORY);
		memset(responseBuffer, 0, 64);
		sprintf(responseBuffer, "%s\n%02X:%02X:%02X:%02X:%02X:%02X\n", "OK", address[0], address[1], address[2], address[3], address[4], address[5]);
		sendResponse(request, responseBuffer, strlen(responseBuffer));
	};

	auto routineReset = [request, this]() { // reset device, reboots
		sendResponse(request, strOK, 4);
		vTaskDelay(1);
		esp_restart();
	};

	auto routineRestore = [request, this]() { // reset device to factory settings
		sendResponse(request, strOK, 4);
		if (jpkNVSWipeRequestCallback != nullptr)
			jpkNVSWipeRequestCallback();
	};

	auto routineSetMode = [request, this]() {
		sendResponse(request, strOK, 4);
		if (strcmp(request->data.command, "AT+MOD=SERVICE") == 0) {
			deviceMode = JoPkaDeviceMode::MODE_SERVICE;
			if (jpkModeChangeRequestCallback != nullptr)
				jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_SERVICE);
		}
		if (strcmp(request->data.command, "AT+MOD=FLASH") == 0) {
			deviceMode = JoPkaDeviceMode::MODE_FLASH;
			if (jpkModeChangeRequestCallback != nullptr)
				jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_FLASH);
		}
		if (strcmp(request->data.command, "AT+MOD=RUN") == 0) {
			deviceMode = JoPkaDeviceMode::MODE_RUN;
			if (jpkModeChangeRequestCallback != nullptr)
				jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_RUN);
		}
		if (strcmp(request->data.command, "AT+MOD=STORAGE") == 0) {
			deviceMode = JoPkaDeviceMode::MODE_STORAGE;
			if (jpkModeChangeRequestCallback != nullptr)
				jpkModeChangeRequestCallback(JoPkaDeviceMode::MODE_STORAGE);
		}
	};
	auto routineGetMode = [request, this]() {
		sendResponse(request, strOK, 4);
		char* mode = JoPkaDeviceModeToString(deviceMode);
		memcpy(responseBuffer, mode, strlen(mode));
		responseBuffer[strlen(mode)] = '\n';
		responseBuffer[strlen(mode) + 1] = '\0';
		sendResponse(request, responseBuffer, strlen(responseBuffer));
	};

	// Determine which command was sent
	if (strncmp(request->data.command, "AT+INF", 6) == 0) // return device info
		routineGetInfo();
	else if (strncmp(request->data.command, "AT+GMR", 6) == 0) // return firmware version
		routineGetFirmwareVersion();
	else if (strncmp(request->data.command, "AT+MAC", 6) == 0) // return device mac
		routineGetMac();
	else if (strncmp(request->data.command, "AT+RST", 6) == 0) // reset device, reboots
		routineReset();
	else if (strncmp(request->data.command, "AT+RES", 6) == 0) // reset device to factory settings
		routineRestore();
	else if (strcmp(request->data.command, "AT+MOD=") == 0) // command to be forwarded to devices such as MURATA
		routineSetMode();
	else if (strcmp(request->data.command, "AT+MOD?") == 0) // command to be forwarded to devices such as MURATA
		routineGetMode();
	else { // unknown command
		sendResponse(request, strERR, 5);
		ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", request->data.command);
	}
}

void JoPkaEndpoint::processRequestLRW(const JoPkaRequest* request)
{
	ESP_LOGE(TAG, "processRequestLRW | Not implemented");
}

uint8_t JoPkaEndpoint::call(uint16_t id)
{
	if (requestsQueue.size() == 0)
		return 0;
#ifdef CONFIG_JPK_ENABLE_LOCKS
	xSemaphoreTake(lock, portMAX_DELAY);
#endif

	JoPkaRequest* request = requestsQueue.front();
	requestsQueue.pop();
	if (requestsQueue.size() != 0) // schedule before processing as to prevent it being scheduled twice in handler of some event
		TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_JOPKA_PROCESS, (uint64_t)0, 0, TaskPriority::TSK_PRIORITY_HIGH));

#ifdef CONFIG_JPK_ENABLE_LOCKS
	xSemaphoreGive(lock);
#endif

	switch (request->format) {
	case JoPkaDataFormat::JSN:
		processRequestJSN(request);
		break;
	case JoPkaDataFormat::ATC:
		processRequestATC(request);
		break;
	case JoPkaDataFormat::LRW:
		processRequestLRW(request);
		break;
	}
	delete request;

	return 0;
}
