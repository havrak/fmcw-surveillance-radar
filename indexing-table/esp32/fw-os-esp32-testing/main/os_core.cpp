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

#define STEPPER1_PULSE_GPIO (gpio_num_t)22
#define STEPPER1_DIR_GPIO (gpio_num_t)23
#define STEPPER2_PULSE_GPIO (gpio_num_t)18
#define STEPPER2_DIR_GPIO (gpio_num_t)19


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
mcpwm_oper_handle_t oper1 = NULL;
mcpwm_oper_handle_t oper2 = NULL;
mcpwm_cmpr_handle_t comparator1 = NULL;
mcpwm_cmpr_handle_t comparator2 = NULL;
mcpwm_gen_handle_t generator1 = NULL;
mcpwm_gen_handle_t generator2 = NULL;

// Timer handles
OSCore::OSCore()
{
}

// ISR to handle pulse counting for stepper 1
bool IRAM_ATTR pulse_count_handler_stepper1(gptimer_handle_t timer, const void *args) {
    pulse_count_stepper1++;
    if (pulse_count_stepper1 >= stepper1_command.steps) {
        mcpwm_timer_stop(timer1);
        stepper1_command.complete = true;
        pulse_count_stepper1 = 0;
        xEventGroupSetBits(stepper_event_group, STEPPER_COMPLETE_BIT_1);
    }
    return true;  // Return true to auto-clear the interrupt
}

// ISR to handle pulse counting for stepper 2
bool IRAM_ATTR pulse_count_handler_stepper2(gptimer_handle_t timer, const void *args) {
    pulse_count_stepper2++;
    if (pulse_count_stepper2 >= stepper2_command.steps) {
        mcpwm_timer_stop(timer2);
        stepper2_command.complete = true;
        pulse_count_stepper2 = 0;
        xEventGroupSetBits(stepper_event_group, STEPPER_COMPLETE_BIT_2);
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
        .period_ticks = 1000,      // Will adjust for RPM
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    mcpwm_new_timer(&timer1_config, &timer1);

    // Configure MCPWM timer for stepper 2
    mcpwm_timer_config_t timer2_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .period_ticks = 1000,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    mcpwm_new_timer(&timer2_config, &timer2);

    // Set up MCPWM oper for each stepper
    mcpwm_oper_config_t oper_config = {.group_id = MCPWM_GROUP_0};
    mcpwm_new_oper(&oper_config, &oper1);
    mcpwm_new_oper(&oper_config, &oper2);

    // Connect the opers to timers
    mcpwm_oper_connect_timer(oper1, timer1);
    mcpwm_oper_connect_timer(oper2, timer2);

    // Set up comparator and generator for each stepper
    mcpwm_comparator_config_t comparator_config = {.flags.update_cmp_on_tez = true};
    mcpwm_new_comparator(oper1, &comparator_config, &comparator1);
    mcpwm_new_comparator(oper2, &comparator_config, &comparator2);

    mcpwm_generator_config_t generator_config_1 = {.gen_gpio_num = STEPPER1_PULSE_GPIO};
    mcpwm_generator_config_t generator_config_2 = {.gen_gpio_num = STEPPER2_PULSE_GPIO};
    mcpwm_new_generator(oper1, &generator_config_1, &generator1);
    mcpwm_new_generator(oper2, &generator_config_2, &generator2);

    // Configure the generator actions
    mcpwm_generator_set_action_on_timer_event(generator1, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE));
    mcpwm_generator_set_action_on_timer_event(generator2, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE));
}

void pcnt_init(pcnt_unit_t unit, int pulse_gpio, int max_steps, EventBits_t complete_bit) {
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = pulse_gpio,
        .ctrl_gpio_num = -1,   // Not used
        .channel = PCNT_CHANNEL_0,
        .unit = unit,
        .pos_mode = PCNT_COUNT_INC,   // Count on rising edge
        .neg_mode = PCNT_COUNT_DIS,   // Ignore falling edge
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_h_lim = max_steps,
        .counter_l_lim = 0,
    };
    pcnt_unit_config(&pcnt_config);

    // Set up an ISR to trigger when the pulse count limit is reached
    pcnt_event_enable(unit, PCNT_EVT_H_LIM);
    pcnt_isr_register([](void *arg) {
        EventBits_t *complete_bit = (EventBits_t *)arg;
        xEventGroupSetBits(stepper_event_group, *complete_bit);
    }, &complete_bit, ESP_INTR_FLAG_IRAM, NULL);

    pcnt_intr_enable(unit);
}

void stepper_task_1(void *arg) {
    while (1) {
        if (xQueueReceive(command_queue_1, &stepper1_command, portMAX_DELAY)) {
            float pulse_frequency = (stepper1_command.rpm * 200) / 60.0;
            uint32_t period_ticks = (uint32_t)(1000000 / pulse_frequency); // Convert to timer ticks

            stepper1_command.complete = false;

            // Set direction using GPIO
            gpio_set_level(STEPPER1_DIR_GPIO, stepper1_command.direction ? 1 : 0);

            // Reset and start pulse counter
            pcnt_counter_clear(PCNT_UNIT_0);
            pcnt_set_event_value(PCNT_UNIT_0, PCNT_EVT_H_LIM, stepper1_command.steps);

            // Start MCPWM for generating pulses
            mcpwm_timer_set_period(timer1, period_ticks);
            mcpwm_timer_start_stop(timer1, MCPWM_TIMER_START_NO_STOP);
        }
    }
}

// Task to process commands for stepper 2
void stepper_task_2(void *arg) {
    while (1) {
        if (xQueueReceive(command_queue_2, &stepper2_command, portMAX_DELAY)) {
            float pulse_frequency = (stepper2_command.rpm * 200) / 60.0;
            uint32_t period_ticks = (uint32_t)(1000000 / pulse_frequency); // Convert to timer ticks

            stepper2_command.complete = false;

            // Set direction using GPIO
            gpio_set_level(STEPPER2_DIR_GPIO, stepper2_command.direction ? 1 : 0);

            // Reset and start pulse counter
            pcnt_counter_clear(PCNT_UNIT_1);
            pcnt_set_event_value(PCNT_UNIT_1, PCNT_EVT_H_LIM, stepper2_command.steps);

            // Start MCPWM for generating pulses
            mcpwm_timer_set_period(timer2, period_ticks);
            mcpwm_timer_start_stop(timer2, MCPWM_TIMER_START_NO_STOP);
        }
    }
}

// Function to set a command for stepper 1
void set_stepper1_command(uint32_t steps, float rpm, bool direction) {
    stepper_command_t command = {
        .steps = steps,
        .rpm = rpm,
        .direction = direction,
        .complete = false,
    };
    xQueueSend(command_queue_1, &command, portMAX_DELAY);
}

// Function to set a command for stepper 2
void set_stepper2_command(uint32_t steps, float rpm, bool direction) {
    stepper_command_t command = {
        .steps = steps,
        .rpm = rpm,
        .direction = direction,
        .complete = false,
    };
    xQueueSend(command_queue_1, &command, portMAX_DELAY);
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
    timer_init();

    xTaskCreate(stepper_task_1, "Stepper Task 1", 2048, NULL, 5, NULL);
    xTaskCreate(stepper_task_2, "Stepper Task 2", 2048, NULL, 5, NULL);

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

