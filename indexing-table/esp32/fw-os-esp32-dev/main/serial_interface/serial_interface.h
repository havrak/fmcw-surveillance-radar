/*
 * serial_interface.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef JOPKA_ENDPOINT_H
#define JOPKA_ENDPOINT_H

#include <mqtt_observer.h>
#include <driver/uart.h>

#include <mqtt_wrapper.h>
#include <mqtt_singleton_wrapper.h>
#include <time_manager.h>
#include <esp_log.h>
#include <string>
#include <queue>
#include <callback_interface.h>
#include <tasker_singleton_wrapper.h>
#include <jopka_tasker_ids.h>
#include <cJSON.h>

#define EX_UART_NUM UART_NUM_0

#define LENGTH_TO_COPY(x, y) (x+y >= CONFIG_JPK_RS232_BUFFER_SIZE ? CONFIG_JPK_RS232_BUFFER_SIZE - x : y)

#ifdef CONFIG_JPK_ENABLE_LOCKS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

enum JoPkaDeviceMode : uint8_t {
	MODE_SERVICE = 0,
	MODE_FLASH = 1,
	MODE_RUN = 2,
	MODE_STORAGE = 3
};

char* JoPkaDeviceModeToString(JoPkaDeviceMode mode);

enum JoPkaDataFormat : uint8_t {
	JSN = 0, // JSON
	ATC = 1, // AT command
	LRW = 2  // bridge to LoRaWAN modem
};

enum JoPkaDataOrigin : uint8_t {
	MQTT = 0,
	RS232 = 1
};

class JoPkaRequest{
	public:
		JoPkaDataOrigin origin;
		JoPkaDataFormat format;

		union {
			char command[32]; // AT command, Command to be forwarded to devices such as MURATA
			cJSON* jsn; // array of messages
		} data;

		JoPkaRequest(JoPkaDataOrigin origin, JoPkaDataFormat format, const char* command, uint32_t command_len): origin(origin), format(format)
	{
		memset(this->data.command, 0, 32);
		strncpy(this->data.command, command, command_len);
	};

		JoPkaRequest(JoPkaDataOrigin origin, cJSON* jsn): origin(origin) {
			format = JoPkaDataFormat::JSN;
			this->data.jsn = jsn;
		};

		~JoPkaRequest(){
			if(this->format == JoPkaDataFormat::JSN)
				cJSON_Delete(this->data.jsn);
		};

};

class JoPkaEndpoint: CallbackInterface, MQTTObserver {
	private:

		static JoPkaEndpoint* instance;
		static MQTTWrapper* mqttWrapperPtr;

		std::queue<JoPkaRequest*> requestsQueue;
		JoPkaDeviceMode deviceMode = JoPkaDeviceMode::MODE_SERVICE;

		constexpr static char TAG[] = "JoPkaEndpoint";
		static QueueHandle_t uart0_queue;

		char responseBuffer[CONFIG_JPK_RESPONSE_BUFFER_SIZE];

		JoPkaEndpoint(bool mqtt, bool serial);
		JoPkaEndpoint(MQTTWrapper* mqttWrapperPtr, bool serial = false);
		JoPkaEndpoint(bool serial = false);

#ifdef CONFIG_WFM_ENABLE_LOCKS
    SemaphoreHandle_t lock;
#endif


		void setupSerial();
		void subscribeMQTTopics();
		void unsubscribeMQTTopics();

		static void uartEvent(void* pvParameters);

		void processRequestJSN(const JoPkaRequest* request);
		void processRequestATC(const JoPkaRequest* request);
		void processRequestLRW(const JoPkaRequest* request);

		void sendResponse(const JoPkaRequest* request, const char* response, uint16_t responseLength);

		std::function<void()> jpkNVSWipeRequestCallback = NULL;
		std::function<void(JoPkaDeviceMode)> jpkModeChangeRequestCallback = NULL;




	public:

		void setNVSWipeRequestCallback(std::function<void()> callback)
		{
			jpkNVSWipeRequestCallback = callback;
		}
		void setModeChangeRequestCallback(std::function<void(JoPkaDeviceMode)> callback)
		{
			jpkModeChangeRequestCallback = callback;
		}



		/**
		 * creates JoPkaEndpoint instance for singleton design pattern, entire MQTTWrapper is handled by JoPkaEndpoint, thus managing it from outside could lead to seg faults etc.
		 *
		 * @param mqttWrapperPtr - MQTTWrapper instance, connection is handled by toggleMQTT method
		 * @param serial - true to create serial interface
		 */
		static void createInstance(MQTTWrapper* mqttWrapperPtr, bool serial){
			instance = new JoPkaEndpoint(mqttWrapperPtr, serial);
		};


		/**
		 * creates JoPkaEndpoint instance for singleton design pattern
		 *
		 * @param mqtt - true to create mqtt Provider (doesn't connect), parameters are configured via KConfig macros
		 * @param serial - true to create serial interface
		 */
		static void createInstance(bool mqtt, bool serial){
			instance = new JoPkaEndpoint(mqtt, serial);
		};

		/**
		 * returns instance of JoPkaEndpoint, instance needs to be created with createInstance call
		 *
		 * @return JoPkaEndpoint*, nullptr if instance wasn't created
		 */
		static JoPkaEndpoint* getInstance(){
			return instance;
		};


		/**
		 * toggles MQTT connection, also subscribes to topics, upon disconnecting client is destroyed
		 *
		 * @param toggle - true to connect, false to disconnect (destroys whole client)
		 * @return bool - true if successful
		 */
		bool toggleMQTT(bool toggle);

		/**
		 * callback from MQTTObserver, should NOT be called manually
		 */
		void onMQTTEvent(const char* topic, ssize_t topic_len, const char* data, ssize_t data_len) override;

		/**
		 * callback from MQTTObserver, should NOT be called manually
		 */
		void onMQTTConnected();

		/**
		 * callback from CallbackInterface, should NOT be called manually
		 */
		uint8_t call(uint16_t id) override;

		/**
		 * schedules processing of JSN request, data are copied and JSN is parsed
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestJSN(const char* input, uint16_t inputLength, JoPkaDataOrigin origin);

		/**
		 * schedules processing of ATC request, data are copied
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestATC(const char* input, uint16_t inputLength,  JoPkaDataOrigin origin);

		/**
		 * schedules processing of LRW request, data are copied
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestLRW(const char* input,uint16_t inputLength, JoPkaDataOrigin origin);
};

#endif /* !JOPKA_ENDPOINT_H */
