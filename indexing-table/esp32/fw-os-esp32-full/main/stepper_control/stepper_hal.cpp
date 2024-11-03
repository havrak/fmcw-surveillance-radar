/*
 * stepper_hal.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_hal.h"

StepperHal steppers = StepperHal();


bool StepperHal::pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx){
	if(unit == StepperHal::pcntUnitH) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(StepperHal::pcntUnitH, StepperHal::stepperCommandH->steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
		mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to

	} else if(unit == StepperHal::pcntUnitT) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(StepperHal::pcntUnitT, StepperHal::stepperCommandT->steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_T);
		mcpwm_timer_start_stop(StepperHal::timerT, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
	}
	return true;
}

void StepperHal::initMCPWN() {
	// Configure MCPWM timer for stepper 1
	mcpwm_timer_config_t timerH_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 1000000,  // 1 MHz resolution
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,      // Will adjust for RPM
	};
	mcpwm_new_timer(&timerH_config, &StepperHal::timerH);

	// Configure MCPWM timer for stepper 2
	mcpwm_timer_config_t timerT_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 1000000,
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,
	};
	mcpwm_new_timer(&timerT_config, &StepperHal::timerT);

	// Set up MCPWM operator for each stepper
	mcpwm_operator_config_t operatorConfig = {
		.group_id = 0,
		.intr_priority = 0, // as opposed to RMT this will possibly be bothered by other interrupts
	};
	// operator_config.group_id =0;
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &StepperHal::operatorH));
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &StepperHal::operatorT));

	// Connect the operators to timers
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(StepperHal::operatorH, StepperHal::timerH));
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(StepperHal::operatorT, StepperHal::timerT));

	// Set up comparator and generator for each stepper
	mcpwm_comparator_config_t comparator_config;
	comparator_config.flags.update_cmp_on_tez = true;
	ESP_ERROR_CHECK(mcpwm_new_comparator(StepperHal::operatorH, &comparator_config, &StepperHal::comparatorH));
	ESP_ERROR_CHECK(mcpwm_new_comparator(StepperHal::operatorT, &comparator_config, &StepperHal::comparatorT));

	mcpwm_generator_config_t generator_config;
	generator_config.gen_gpio_num = CONFIG_STEPPER_H_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(StepperHal::operatorH, &generator_config, &StepperHal::generatorH));
	generator_config.gen_gpio_num = CONFIG_STEPPER_T_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(StepperHal::operatorT, &generator_config, &StepperHal::generatorT));


	// Configure the generator actions - toggle on timer event
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(StepperHal::generatorH, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(StepperHal::generatorT, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));

	ESP_ERROR_CHECK(mcpwm_timer_enable(StepperHal::timerH));
	ESP_ERROR_CHECK(mcpwm_timer_enable(StepperHal::timerT));
}

void initPCNT(){
	const char *TAG = "PCNT";
	ESP_LOGI(TAG, "Initializing PCNT for pulse count on pin");
	pcnt_unit_config_t unitConfig = {
		.low_limit = -1,
		.high_limit = 32767,
		.intr_priority = 0,
	};
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &StepperHal::pcntUnitT));
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &StepperHal::pcntUnitH));


	ESP_LOGI(TAG, "set glitch filter");
	pcnt_glitch_filter_config_t filterConfig = {
		.max_glitch_ns = 1000,
	};
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(StepperHal::pcntUnitH, &filterConfig));
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(StepperHal::pcntUnitT, &filterConfig));

	pcnt_chan_config_t channelConfig;
	channelConfig.edge_gpio_num = (gpio_num_t) CONFIG_STEPPER_H_PIN_STEP;
	ESP_ERROR_CHECK(pcnt_new_channel(StepperHal::pcntUnitH, &channelConfig, &StepperHal::pcntChanH));
	channelConfig.edge_gpio_num = (gpio_num_t) CONFIG_STEPPER_T_PIN_STEP;
	ESP_ERROR_CHECK(pcnt_new_channel(StepperHal::pcntUnitT, &channelConfig, &StepperHal::pcntChanT));


	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(StepperHal::pcntChanH, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling
	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(StepperHal::pcntChanT, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling

	pcnt_event_callbacks_t cbs = {
		.on_reach = StepperHal::pcntOnReach,
	};

	StepperHal::pcntQueueH = xQueueCreate(10, sizeof(int));
	StepperHal::pcntQueueT = xQueueCreate(10, sizeof(int));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(StepperHal::pcntUnitH, &cbs, StepperHal::pcntQueueH));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(StepperHal::pcntUnitT, &cbs, StepperHal::pcntQueueT));

	ESP_LOGI(TAG, "enable pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_enable(StepperHal::pcntUnitH));
	ESP_ERROR_CHECK(pcnt_unit_enable(StepperHal::pcntUnitT));
	ESP_LOGI(TAG, "clear pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_clear_count(StepperHal::pcntUnitH));
	ESP_ERROR_CHECK(pcnt_unit_clear_count(StepperHal::pcntUnitT));
	ESP_LOGI(TAG, "start pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_start(StepperHal::pcntUnitH));
	ESP_ERROR_CHECK(pcnt_unit_start(StepperHal::pcntUnitT));
}

void StepperHal::stepperTaskH(void *arg) {
	while (1) {
		if (xQueueReceive(StepperHal::commandQueueH, StepperHal::stepperCommandH, portMAX_DELAY)) {
			// if previous command was spindle, we are running a command that will change stepper movement we need to immediately set spindle regime end time
			if(StepperHal::stepperCommandPrevH->type == CommandType::SPINDLE && (StepperHal::stepperCommandH->type & 0x0F) < CommandType::SKIP){
				StepperHal::stepperCommandPrevH->val.finishTime = esp_timer_get_time();
			}

			ESP_LOGI("Stepper 1", "Received command for stepper 1");
			uint32_t period_ticks = (uint32_t)(60'000'000/100/StepperHal::stepperCommandH->rpm); // Convert to timer ticks (as we are toggling on timer event we need to double the RPM)
			// ESP_LOGI("Stepper 1", "Period ticks: %ld", period_ticks);
			// Set direction using GPIO
			gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_DIR, StepperHal::stepperCommandH->direction ? 1 : 0);

			// Reset and start pulse counter
			switch(StepperHal::stepperCommandH->type){
				case CommandType::STEPPER_INDIVIDUAL:
				case CommandType::STEPPER_SYNCHRONIZED:
					StepperHal::stepperCommandH->timestamp = esp_timer_get_time();
					StepperHal::stepperCommandH->complete = false;
					ESP_ERROR_CHECK(pcnt_unit_add_watch_point(StepperHal::pcntUnitH, StepperHal::stepperCommandH->val.steps));
					ESP_ERROR_CHECK(pcnt_unit_clear_count(StepperHal::pcntUnitH));
					mcpwm_timer_set_period(StepperHal::timerH, period_ticks);
					mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_NO_STOP);
					break;
				case CommandType::SPINDLE:
				case CommandType::SPINDLE_SYNCHRONIZED:
					StepperHal::stepperCommandH->timestamp = esp_timer_get_time();
					StepperHal::stepperCommandH->complete = true;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					mcpwm_timer_set_period(StepperHal::timerH, period_ticks);
					mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_NO_STOP);
					break;
				case CommandType::SKIP:
					StepperHal::stepperCommandH->complete = true;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;
				case CommandType::WAIT:
					vTaskDelay(StepperHal::stepperCommandH->val.time / portTICK_PERIOD_MS);
					StepperHal::stepperCommandH->complete = true;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;
				case CommandType::STOP:
					StepperHal::stepperCommandH->complete = true;
					mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_STOP_FULL);
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;

			}
			// TODO freeze execution until the bit is set
			EventBits_t result = xEventGroupWaitBits(
					stepper_event_group,
					(StepperHal::stepperCommandH->type >> 4) ? STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T : STEPPER_COMPLETE_BIT_H,
					pdTRUE,
					pdTRUE,
					portMAX_DELAY
					);

			// --> move current command to old command
			if((StepperHal::stepperCommandPrevH->type 0x0F) < CommandType::STOP) // only copy commands that result in movement
				memcpy(StepperHal::stepperCommandPrevH, StepperHal::stepperCommandH, sizeof(stepper_command_t));
		}
	}
}

// Task to process commands for stepper 2
void StepperHal::stepperTaskT(void *arg) {
	while (1) {
		if (xQueueReceive(StepperHal::commandQueueT, StepperHal::stepperCommandT, portMAX_DELAY)) {
			ESP_LOGI("Stepper 2", "Received command for stepper 2");
			uint32_t period_ticks = (uint32_t)(60'000'000/100/StepperHal::stepperCommandT->rpm); // Convert to timer ticks
			ESP_LOGI("Stepper 2", "Period ticks: %ld", period_ticks);

			StepperHal::stepperCommandT->complete = false;

			// Set direction using GPIO
			gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN_DIR, StepperHal::stepperCommandT->direction ? 1 : 0);

			// Reset and start pulse counter
			ESP_ERROR_CHECK(pcnt_unit_add_watch_point(StepperHal::pcntUnitT, StepperHal::stepperCommandT->steps));
			ESP_ERROR_CHECK(pcnt_unit_clear_count(StepperHal::pcntUnitT));
			// pcnt_set_event_value(PCNT_UNIT_1, PCNT_EVT_H_LIM, stepperCommandT->steps);

			// Start MCPWM for generating pulses
			mcpwm_timer_set_period(StepperHal::timerT, period_ticks);
			mcpwm_timer_start_stop(StepperHal::timerT, MCPWM_TIMER_START_NO_STOP);

			EventBits_t result = xEventGroupWaitBits(
					stepper_event_group,
					StepperHal::stepperCommandT->type == CommandType::SYNCHRONIZED ? STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T : STEPPER_COMPLETE_BIT_T,
					pdTRUE,
					pdTRUE,
					portMAX_DELAY
					);

		}
	}
}

bool StepperHal::setStepper1Command(int32_t steps, float rpm) {
	if(steps > 32767 || steps < -32767) {
		ESP_LOGE("Stepper", "Step count out of range");
		return false;
	}
	stepper_command_t command = {
		.type = CommandType::INDIVIDUAL,
		.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
	};
	xQueueSend(commandQueueH, &command, portMAX_DELAY);
	return true;
}

// Function to set a command for stepper 2
bool StepperHal::setStepper2Command(int32_t steps, float rpm) {

	if(steps > 32767 || steps < -32767) {
		ESP_LOGE("Stepper", "Step count out of range");
		return false;
	}
	stepper_command_t command = {
		.type = CommandType::INDIVIDUAL,
		.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
	};
	xQueueSend(commandQueueT, &command, portMAX_DELAY);
	return true;
}

bool StepperHal::setSteppersCommand(int32_t steps1, float rpm1, int32_t steps2, float rpm2) {

	if(steps1 > 32767 || steps1 < -32767 || steps2 > 32767 || steps2 < -32767) {
		ESP_LOGE("Stepper", "Step count out of range");
		return false;
	}
	stepper_command_t command1 = {
		.type = CommandType::SYNCHRONIZED,
		.steps = steps1 >= 0 ? (uint32_t) steps1 : ((uint32_t ) -steps1),
		.rpm = rpm1,
		.direction = steps1 >= 0,
		.complete = false,
	};
	stepper_command_t command2 = {
		.type = CommandType::SYNCHRONIZED,
		.steps = steps2 >= 0 ? (uint32_t) steps2 : ((uint32_t ) -steps2),
		.rpm = rpm2,
		.direction = steps2 >= 0,
		.complete = false,
	};
	xQueueSend(commandQueueH, &command1, portMAX_DELAY);
	xQueueSend(commandQueueT, &command2, portMAX_DELAY);
	return true;
}

