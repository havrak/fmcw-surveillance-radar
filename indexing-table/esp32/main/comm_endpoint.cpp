/*
 * comm_endpoint.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "comm_endpoint.h"

CommEndpoint commEndpoint = CommEndpoint();
CommEndpoint* CommEndpoint::instance = nullptr;

CommEndpoint::CommEndpoint()
{
}


void CommEndpoint::uartEvent(void* commEndpointInstance) // TODO pin to core 0
{

	static uart_event_t event;
	static uint8_t* dtmp = (uint8_t*)malloc(CONFIG_COMM_RS232_BUFFER_SIZE);

	static char buffer[CONFIG_COMM_RS232_BUFFER_SIZE + 2];
	static int i = 0;
	static int j = 0;
	static int j_prev = 0;
	static char* pointer = 0;
	static uint32_t startIndex;
	i = 0;
	j = 0;
	j_prev = 0;
	pointer = 0;
	while (1) {
		if (xQueueReceive(uart0_queue, (void*)&event, (TickType_t)portMAX_DELAY)) {
			memset(dtmp, 0, sizeof(CONFIG_COMM_RS232_BUFFER_SIZE));
			if (event.type == UART_DATA) {
				uart_read_bytes(UART_NUM_0, dtmp, event.size, portMAX_DELAY);

				for (j = 0, j_prev = 0; j < event.size; j++) {
					char c = dtmp[j];
					if ((int)dtmp[j] == 13) {   // match carriage return
						memcpy(buffer + i, dtmp + j_prev, LENGTH_TO_COPY(i, j - j_prev));
						i += LENGTH_TO_COPY(i, j - j_prev);
						buffer[i] = '\0';

						startIndex = 0;
						while(buffer[startIndex] == ' ' || buffer[startIndex] == '\t')
							startIndex++;


						pointer = buffer + startIndex;
						ParsingGCodeResult result = stepperControl.parseGCode(pointer, strlen(pointer));
						char response[12];
						if(result == ParsingGCodeResult::SUCCESS)
							sprintf(response, "!R OK\r");
						else
							sprintf(response, "!R ERR %d\r", result);

						uart_write_bytes(UART_NUM_0, response, strlen(response));

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
	ESP_LOGI(TAG, "setupComm | CONFIG_COMM_RS232_BAUDRATE: %d", CONFIG_COMM_RS232_BAUDRATE);
	ESP_LOGI(TAG, "setupComm | CONFIG_COMM_RS232_BUFFER_SIZE: %d", CONFIG_COMM_RS232_BUFFER_SIZE);

	esp_err_t err;
	uart_config_t uart_config = {
		.baud_rate = CONFIG_COMM_RS232_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	// Install UART driver, and get the queue.
	uart_driver_install(UART_NUM_0, CONFIG_COMM_RS232_BUFFER_SIZE * 2, 256, 20, &uart0_queue, 0);

	err=uart_param_config(UART_NUM_0, &uart_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "setupComm | uart_param_config failed");
		uart_driver_delete(UART_NUM_0);
		return false;
	}
	err=uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "setupComm | uart_set_pin failed");
		uart_driver_delete(UART_NUM_0);
		return false;
	}

	// Create a task to handler UART event from ISR
	xTaskCreate(uartEvent, "uartEvent", 8192, NULL, 12, NULL);
	return true;
}


