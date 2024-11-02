/*
 * os_core.cpp
 * Copyright (C) 2023 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include <os_core.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"

#define STEPPER1_DIR_GPIO (gpio_num_t)23
#define STEPPER1_PULSE_GPIO (gpio_num_t)22
#define STEPPER1_PULSE_SENSE_GPIO (gpio_num_t)21
#define STEPPER2_DIR_GPIO (gpio_num_t)20
#define STEPPER2_PULSE_GPIO (gpio_num_t)19
#define STEPPER2_PULSE_SENSE_GPIO (gpio_num_t)18


#define STEPPER_COMPLETE_BIT_1 BIT0
#define STEPPER_COMPLETE_BIT_2 BIT1

OSCore* OSCore::instance = nullptr;

typedef struct {
	uint32_t steps;
	float rpm;
	bool direction; // true = forward, false = backward
	bool complete;
} stepper_command_t;

volatile uint32_t pulse_count_stepper1 = 0;
volatile uint32_t pulse_count_stepper2 = 0;
stepper_command_t stepper1_command;
stepper_command_t stepper2_command;
QueueHandle_t command_queue_1;
QueueHandle_t command_queue_2;
EventGroupHandle_t stepper_event_group;

// MCPWM opers
mcpwm_timer_handle_t timer1 = NULL;
mcpwm_timer_handle_t timer2 = NULL;
mcpwm_oper_handle_t operator1 = NULL;
mcpwm_oper_handle_t operator2 = NULL;
mcpwm_cmpr_handle_t comparator1 = NULL;
mcpwm_cmpr_handle_t comparator2 = NULL;
mcpwm_gen_handle_t generator1 = NULL;
mcpwm_gen_handle_t generator2 = NULL;

// PCNT units
pcnt_unit_handle_t pcnt_unit1 = NULL;
pcnt_unit_handle_t pcnt_unit2 = NULL;
pcnt_channel_handle_t pcnt_chan1 = NULL;
pcnt_channel_handle_t pcnt_chan2 = NULL;
QueueHandle_t pcnt_queue1;
QueueHandle_t pcnt_queue2;

// Timer handles
OSCore::OSCore()
{
}

static bool pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
	if(unit == pcnt_unit1) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(pcnt_unit1, stepper1_command.steps));
		xEventGroupSetBits(stepper_event_group, STEPPER_COMPLETE_BIT_1);
		mcpwm_timer_start_stop(timer1, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to

	} else if(unit == pcnt_unit2) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(pcnt_unit2, stepper2_command.steps));
		xEventGroupSetBits(stepper_event_group, STEPPER_COMPLETE_BIT_2);
		mcpwm_timer_start_stop(timer2, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
	}
	return true;
}


// Function to initialize MCPWM for each stepper
void stepper_mcpwm_init() {
	// Configure MCPWM timer for stepper 1
	mcpwm_timer_config_t timer1_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 1000000,  // 1 MHz resolution
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,      // Will adjust for RPM
	};
	mcpwm_new_timer(&timer1_config, &timer1);

	// Configure MCPWM timer for stepper 2
	mcpwm_timer_config_t timer2_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 1000000,
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,
	};
	mcpwm_new_timer(&timer2_config, &timer2);

	// Set up MCPWM operator for each stepper
	mcpwm_operator_config_t operator_config = {
		.group_id = 0,
		.intr_priority = 0, // as opposed to RMT this will possibly be bothered by other interrupts
	};
	// operator_config.group_id =0;
	ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &operator1));
	ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &operator2));

	// Connect the operators to timers
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(operator1, timer1));
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(operator2, timer2));

	// Set up comparator and generator for each stepper
	mcpwm_comparator_config_t comparator_config;
	comparator_config.flags.update_cmp_on_tez = true;
	ESP_ERROR_CHECK(mcpwm_new_comparator(operator1, &comparator_config, &comparator1));
	ESP_ERROR_CHECK(mcpwm_new_comparator(operator2, &comparator_config, &comparator2));

	mcpwm_generator_config_t generator_config;
	generator_config.gen_gpio_num = STEPPER1_PULSE_GPIO;
	ESP_ERROR_CHECK(mcpwm_new_generator(operator1, &generator_config, &generator1));
	generator_config.gen_gpio_num = STEPPER2_PULSE_GPIO;
	ESP_ERROR_CHECK(mcpwm_new_generator(operator2, &generator_config, &generator2));


	// Configure the generator actions - toggle on timer event
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator1, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator2, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));

	ESP_ERROR_CHECK(mcpwm_timer_enable(timer1));
	ESP_ERROR_CHECK(mcpwm_timer_enable(timer2));

}

void pcnt_init() {
	const char *TAG = "PCNT";
	ESP_LOGI(TAG, "Initializing PCNT for pulse count on pin");
	pcnt_unit_config_t unit_config = {
		.low_limit = -1,
		.high_limit = 32767,
		.intr_priority = 0,
	};
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit2));
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit1));


	ESP_LOGI(TAG, "set glitch filter");
	pcnt_glitch_filter_config_t filter_config = {
		.max_glitch_ns = 1000,
	};
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit1, &filter_config));
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit2, &filter_config));

	pcnt_chan_config_t channel_config;
	channel_config.edge_gpio_num = STEPPER1_PULSE_SENSE_GPIO;
	ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit1, &channel_config, &pcnt_chan1));
	channel_config.edge_gpio_num = STEPPER2_PULSE_SENSE_GPIO;
	ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit2, &channel_config, &pcnt_chan2));


	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan1, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling
	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan2, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling

	pcnt_event_callbacks_t cbs = {
		.on_reach = pcnt_on_reach,
	};

	pcnt_queue1 = xQueueCreate(10, sizeof(int));
	pcnt_queue2 = xQueueCreate(10, sizeof(int));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit1, &cbs, pcnt_queue1));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit2, &cbs, pcnt_queue2));

	ESP_LOGI(TAG, "enable pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit1));
	ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit2));
	ESP_LOGI(TAG, "clear pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit1));
	ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit2));
	ESP_LOGI(TAG, "start pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit1));
	ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit2));
}

void stepper_task_1(void *arg) {
	while (1) {
		if (xQueueReceive(command_queue_1, &stepper1_command, portMAX_DELAY)) {
			ESP_LOGI("Stepper 1", "Received command for stepper 1");
			uint32_t period_ticks = (uint32_t)(60'000'000/100/stepper1_command.rpm); // Convert to timer ticks (as we are toggling on timer event we need to double the RPM)
			ESP_LOGI("Stepper 1", "Period ticks: %ld", period_ticks);

			stepper1_command.complete = false;

			// Set direction using GPIO
			gpio_set_level(STEPPER1_DIR_GPIO, stepper1_command.direction ? 1 : 0);

			// Reset and start pulse counter
			ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit1, stepper1_command.steps));
			ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit1));

			// pcnt_set_event_value(PCNT_UNIT_0, PCNT_EVT_H_LIM, stepper1_command.steps);

			// Start MCPWM for generating pulses
			mcpwm_timer_set_period(timer1, period_ticks);
			mcpwm_timer_start_stop(timer1, MCPWM_TIMER_START_NO_STOP); // won't stop until we tell it to

			// TODO freeze execution until the bit is set
		}
	}
}

// Task to process commands for stepper 2
void stepper_task_2(void *arg) {
	while (1) {
		if (xQueueReceive(command_queue_2, &stepper2_command, portMAX_DELAY ) {
			ESP_LOGI("Stepper 2", "Received command for stepper 2");
			uint32_t period_ticks = (uint32_t)(60'000'000/100/stepper2_command.rpm); // Convert to timer ticks
			ESP_LOGI("Stepper 2", "Period ticks: %ld", period_ticks);

			stepper2_command.complete = false;

			// Set direction using GPIO
			gpio_set_level(STEPPER2_DIR_GPIO, stepper2_command.direction ? 1 : 0);

			// Reset and start pulse counter
			ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit2, stepper2_command.steps));
			ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit2));
			// pcnt_set_event_value(PCNT_UNIT_1, PCNT_EVT_H_LIM, stepper2_command.steps);

			// Start MCPWM for generating pulses
			mcpwm_timer_set_period(timer2, period_ticks);
			mcpwm_timer_start_stop(timer2, MCPWM_TIMER_START_NO_STOP);

			// TODO freeze execution until the bit is set

		}
	}
}

// Function to set a command for stepper 1
bool set_stepper1_command(int32_t steps, float rpm) {
	if(steps > 32767 || steps < -32767) {
		ESP_LOGE("Stepper", "Step count out of range");
		return false;
	}
	stepper_command_t command = {
		.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
	};
	xQueueSend(command_queue_1, &command, portMAX_DELAY);
	return true;
}

// Function to set a command for stepper 2
bool set_stepper2_command(int32_t steps, float rpm) {

	if(steps > 32767 || steps < -32767) {
		ESP_LOGE("Stepper", "Step count out of range");
		return false;
	}
	stepper_command_t command = {
		.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
	};
	xQueueSend(command_queue_2, &command, portMAX_DELAY);
	return true;
}


void OSCore::init()
{

	if (instance != nullptr)
		return;
	instance = new OSCore();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
}

void OSCore::loop()
{
	ESP_LOGI(TAG, "Main | loop | loop started");
	do {
		TaskerSingletonWrapper::getInstance()->tick();
		vTaskDelay(1);
	} while (true);
}

void OSCore::setup()
{
	Wire.begin();


	// ------------------------------------------
	// WiFi
	// ------------------------------------------
	// ESP_LOGI(TAG, "setup | WiFi");
	// WiFiManager::getInstance()->setOnConnectionEstablished(std::bind(&OSCore::connectionToAPEstablished, this));
	// WiFiManager::getInstance()->setOnConnectionLost(std::bind(&OSCore::connectionToAPLost, this));
	// WiFiManager::getInstance()->setRegime(WiFiRegime::WIFI_REGIME_STATION);

	// ------------------------------------------
	// Time Manager
	// ------------------------------------------


	// ------------------------------------------
	// Peripherals
	// ------------------------------------------

	// ------------------------------------------
	// Configure Peripherals
	// ------------------------------------------

	// ------------------------------------------
	// Configure Peripherals
	// ------------------------------------------
	gpio_reset_pin(STEPPER1_DIR_GPIO);
	gpio_reset_pin(STEPPER2_DIR_GPIO);
	gpio_set_direction(STEPPER1_DIR_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_direction(STEPPER2_DIR_GPIO, GPIO_MODE_OUTPUT);

	command_queue_1 = xQueueCreate(5, sizeof(stepper_command_t));
	command_queue_2 = xQueueCreate(5, sizeof(stepper_command_t));
	stepper_event_group = xEventGroupCreate();

	stepper_mcpwm_init();


	// while(1){
	//	vTaskDelay(1000 / portTICK_PERIOD_MS);
	//	asm("nop");
	// }
	//

	pcnt_init();


	xTaskCreate(stepper_task_1, "Stepper Task 1", 2048, NULL, 5, NULL);
	xTaskCreate(stepper_task_2, "Stepper Task 2", 2048, NULL, 5, NULL);


	while(1) { // results in roughly 13 ms delay between iterations
		set_stepper1_command(500, 120.0);
		set_stepper2_command(-500, 120.0);
		EventBits_t result = xEventGroupWaitBits(
				stepper_event_group,
				STEPPER_COMPLETE_BIT_1 | STEPPER_COMPLETE_BIT_2,
				pdTRUE,
				pdTRUE,
				portMAX_DELAY
				);

		if ((result & (STEPPER_COMPLETE_BIT_1 | STEPPER_COMPLETE_BIT_2)) == (STEPPER_COMPLETE_BIT_1 | STEPPER_COMPLETE_BIT_2)) {
			ESP_LOGI("Stepper", "Both steppers completed");
		}
	}
	// ------------------------------------------
	// OSCora Tasker calls setup
	// ------------------------------------------
	TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_OS_CORE_TEST,10'000, 0, TaskPriority::TSK_PRIORITY_MEDIUM));

	// END
	ESP_LOGI(TAG, "setup | setup finished");
}

uint8_t OSCore::call(uint16_t id)
{
	TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_OS_CORE_TEST,10'000 , 0, TaskPriority::TSK_PRIORITY_MEDIUM));
	return 0;
}

void OSCore::onTaskerEvent(CallbackInterface* eventSourceOwner, uint16_t eventSourceId, uint8_t code)
{
	return;
}

void OSCore::onModbusSlaveEvent(mb_event_group_t eventGroup, uint8_t offset, uint8_t size, void* ptr)
{
	return;
}

