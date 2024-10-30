/*
 * comm_endpoint.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef COMM_ENDPOINT_H
#define COMM_ENDPOINT_H

#include <mqtt_observer.h>
#include <driver/uart.h>

#include <mqtt_wrapper.h>
#include <mqtt_singleton_wrapper.h>
#include <esp_log.h>
#include <string>
#include <queue>
#include <callback_interface.h>
#include <tasker_singleton_wrapper.h>
#include <os_core_tasker_ids.h>
#include <motor_control.h>

#define EX_UART_NUM UART_NUM_0
#define MAX_COMMAND_LENGTH 64

#define LENGTH_TO_COPY(x, y) (x+y >= CONFIG_COMM_RS232_BUFFER_SIZE ? CONFIG_COMM_RS232_BUFFER_SIZE - x : y)

#ifdef CONFIG_COMM_ENABLE_LOCKS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif


enum CommDataFormat : uint8_t {
	GCD = 0, // gcode
	ATC = 1, // AT command
};

enum CommDataOrigin : uint8_t {
	MQTT = 0,
	RS232 = 1
};

class CommRequest{
	public:
		CommDataOrigin origin;
		CommDataFormat format;

		char command[MAX_COMMAND_LENGTH]; // AT, gcode command
		bool error;

		CommRequest(CommDataOrigin origin, CommDataFormat format, const char* command, uint32_t command_len): origin(origin), format(format)
	{
		memset(this->command, 0, MAX_COMMAND_LENGTH);
		strncpy(this->command, command, command_len);
	};



};

class CommEndpoint: CallbackInterface, MQTTObserver {
	private:

		static CommEndpoint* instance;
		static MQTTWrapper* mqttWrapperPtr;

		std::queue<CommRequest*> requestsQueue;

		constexpr static char TAG[] = "CommEndpoint";
		static QueueHandle_t uart0_queue;

		char responseBuffer[CONFIG_COMM_RS232_BUFFER_SIZE];

		CommEndpoint(bool mqtt, bool serial);
		CommEndpoint(MQTTWrapper* mqttWrapperPtr, bool serial = false);
		CommEndpoint(bool serial = false);

#ifdef CONFIG_WFM_ENABLE_LOCKS
    SemaphoreHandle_t lock;
#endif


		bool setupComm();
		void subscribeMQTTopics();
		void unsubscribeMQTTopics();

		static void uartEvent(void* pvParameters);

		void processRequestGCD(const CommRequest* request);
		void processRequestATC(const CommRequest* request);

		void sendResponse(const CommRequest* request, const char* response, uint16_t responseLength);



	public:




		/**
		 * creates CommEndpoint instance for singleton design pattern, entire MQTTWrapper is handled by CommEndpoint, thus managing it from outside could lead to seg faults etc.
		 *
		 * @param mqttWrapperPtr - MQTTWrapper instance, connection is handled by toggleMQTT method
		 * @param serial - true to create serial interface
		 */
		static void createInstance(MQTTWrapper* mqttWrapperPtr, bool serial){
			instance = new CommEndpoint(mqttWrapperPtr, serial);
		};


		/**
		 * creates CommEndpoint instance for singleton design pattern
		 *
		 * @param mqtt - true to create mqtt Provider (doesn't connect), parameters are configured via KConfig macros
		 * @param serial - true to create serial interface
		 */
		static void createInstance(bool mqtt, bool serial){
			instance = new CommEndpoint(mqtt, serial);
		};

		/**
		 * returns instance of CommEndpoint, instance needs to be created with createInstance call
		 *
		 * @return CommEndpoint*, nullptr if instance wasn't created
		 */
		static CommEndpoint* getInstance(){
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
		 * schedules processing of GCD request, data are copied and GCD is parsed
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestGCD(const char* input, uint16_t inputLength, CommDataOrigin origin);

		/**
		 * schedules processing of ATC request, data are copied
		 *
		 * @param input - input string
		 * @param origin - origin of data
		 * @return bool - true if successful (Tasker was able to schedule task)
		 */
		bool logRequestATC(const char* input, uint16_t inputLength,  CommDataOrigin origin);

};

#endif /* !COMM_ENDPOINT_H */
