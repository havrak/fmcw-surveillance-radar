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
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(StepperHal::pcntUnitH, StepperHal::stepperCommandH->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
		mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to

	} else if(unit == StepperHal::pcntUnitT) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(StepperHal::pcntUnitT, StepperHal::stepperCommandT->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_T);
		mcpwm_timer_start_stop(StepperHal::timerT, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
	}
	return true;
}

void StepperHal::initMCPWN() {
	// Configure MCPWM timer for stepper 1
	commandQueueH = xQueueCreate(10, sizeof(stepper_command_t));
	commandQueueT = xQueueCreate(10, sizeof(stepper_command_t));
	stepperEventGroup = xEventGroupCreate();

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
	mcpwm_comparator_config_t comparator_config = {
		.intr_priority = 0,
		.flags = {
			.update_cmp_on_tez = true,
		},
	};
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

void StepperHal::initPCNT(){
	const char *TAG = "PCNT";
	ESP_LOGI(TAG, "Initializing PCNT for pulse counters");
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
			if(StepperHal::stepperCommandPrevH->type == CommandType::SPINDLE && StepperHal::stepperCommandH->type < CommandType::SKIP)
				StepperHal::stepperCommandPrevH->val.finishTime = esp_timer_get_time();


			ESP_LOGI("Stepper 1", "Received command for stepper 1");
			uint32_t period_ticks = (uint32_t)(60'000'000/CONFIG_STEPPER_H_STEP_COUNT/StepperHal::stepperCommandH->rpm); // Convert to timer ticks (as we are toggling on timer event we need to double the RPM)
																																																																							 // ESP_LOGI("Stepper 1", "Period ticks: %ld", period_ticks);
																																																																							 // Set direction using GPIO
			gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_DIR, StepperHal::stepperCommandH->direction ? 1 : 0);

			// Reset and start pulse counter
			switch(StepperHal::stepperCommandH->type){
				case CommandType::STEPPER:
					StepperHal::stepperCommandH->timestamp = esp_timer_get_time();
					StepperHal::stepperCommandH->complete = false;
					ESP_ERROR_CHECK(pcnt_unit_add_watch_point(StepperHal::pcntUnitH, StepperHal::stepperCommandH->val.steps));
					ESP_ERROR_CHECK(pcnt_unit_clear_count(StepperHal::pcntUnitH));
					mcpwm_timer_set_period(StepperHal::timerH, period_ticks);
					mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_NO_STOP);
					break;
				case CommandType::SPINDLE:
					StepperHal::stepperCommandH->timestamp = esp_timer_get_time();
					StepperHal::stepperCommandH->complete = false;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					mcpwm_timer_set_period(StepperHal::timerH, period_ticks);
					mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_NO_STOP);
					vTaskDelay(CONFIG_STEPPER_MIN_SPINDLE_TIME/portTICK_PERIOD_MS); // NOTE: necessary delay to make sure information about previous command is read, if not present it would significantly complicate code
					break;
				case CommandType::SKIP:
					StepperHal::stepperCommandH->complete = false;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;
				case CommandType::WAIT:
					StepperHal::stepperCommandH->complete = false;
					vTaskDelay(StepperHal::stepperCommandH->val.time / portTICK_PERIOD_MS);
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;
				case CommandType::STOP:
					StepperHal::stepperCommandH->complete = false;
					mcpwm_timer_start_stop(StepperHal::timerH, MCPWM_TIMER_START_STOP_FULL);
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;

			}
			// TODO freeze execution until the bit is set
			EventBits_t result = xEventGroupWaitBits(
					stepperEventGroup,
					(StepperHal::stepperCommandH->synchronized) ? STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T : STEPPER_COMPLETE_BIT_H,
					pdTRUE, // TODO: need to verify this, should be fine according to https://forums.freertos.org/t/eventgroup-bit-clearing-when-multiple-tasks-wait-for-bit/7599
					pdTRUE,
					portMAX_DELAY
					);
			StepperHal::stepperCommandH->complete = true;

			// here we need to make sure that second task has
			// if(StepperHal::stepperCommandH->synchronized){
			//	while(!StepperHal::stepperCommandT->complete){
			//		asm("nop");
			//	}
			// }

			if(StepperHal::stepperCommandH->type < CommandType::STOP)
				memcpy(StepperHal::stepperCommandPrevH, StepperHal::stepperCommandH, sizeof(stepper_command_t));
			stepperCommandPrevH->synchronized = false;
		}
	}
}

// Task to process commands for stepper 2
void StepperHal::stepperTaskT(void *arg) {
	while (1) {
		if (xQueueReceive(StepperHal::commandQueueT, StepperHal::stepperCommandT, portMAX_DELAY)) {
			ESP_LOGI("Stepper 2", "Received command for stepper 2");
			uint32_t period_ticks = (uint32_t)(60'000'000/CONFIG_STEPPER_H_STEP_COUNT/StepperHal::stepperCommandT->rpm); // Convert to timer ticks
			ESP_LOGI("Stepper 2", "Period ticks: %ld", period_ticks);

			StepperHal::stepperCommandT->complete = false;

			// Set direction using GPIO
			gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN_DIR, StepperHal::stepperCommandT->direction ? 1 : 0);

			// Reset and start pulse counter
			ESP_ERROR_CHECK(pcnt_unit_add_watch_point(StepperHal::pcntUnitT, StepperHal::stepperCommandT->val.steps));
			ESP_ERROR_CHECK(pcnt_unit_clear_count(StepperHal::pcntUnitT));
			// pcnt_set_event_value(PCNT_UNIT_1, PCNT_EVT_H_LIM, stepperCommandT->steps);

			// Start MCPWM for generating pulses
			mcpwm_timer_set_period(StepperHal::timerT, period_ticks);
			mcpwm_timer_start_stop(StepperHal::timerT, MCPWM_TIMER_START_NO_STOP);

			EventBits_t result = xEventGroupWaitBits(
					stepperEventGroup,
					StepperHal::stepperCommandT->synchronized ? STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T : STEPPER_COMPLETE_BIT_T,
					pdTRUE, // dont't clear bits here as we need them to clear second task
					pdTRUE,
					portMAX_DELAY
					);
			StepperHal::stepperCommandT->complete = true;

			// here we need to make sure that second task has
			// if(StepperHal::stepperCommandT->synchronized){
			//	while(!StepperHal::stepperCommandH->complete){
			//		asm("nop");
			//	}
			// }

		}
	}
}

bool StepperHal::stepStepperH(int16_t steps, float rpm, bool synchronized) {
	stepper_command_t command = {
		.type = steps == 0 ? CommandType::SKIP : CommandType::STEPPER,
		.val = {
			.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		},
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(commandQueueH, &command, portMAX_DELAY)==  pdTRUE;
}

// Function to set a command for stepper 2
bool StepperHal::stepStepperT(int16_t steps, float rpm, bool synchronized) {

	stepper_command_t command = {
		.type = steps == 0 ? CommandType::SKIP : CommandType::STEPPER,
		.val = {
			.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		},
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(commandQueueT, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::waitStepperH(uint32_t time, bool synchronized) {
	stepper_command_t command = {
		.type = CommandType::WAIT,
		.val = {.time = time,},
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(commandQueueH, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::waitStepperT(uint32_t time, bool synchronized) {
	stepper_command_t command = {
		.type = CommandType::WAIT,
		.val= {.time = time,},
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(commandQueueT, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::spindleStepperH(float rpm, Direction direction) {
	stepper_command_t command = {
		.type = CommandType::SPINDLE,
		.rpm = rpm,
		.direction = direction,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(commandQueueH, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::spindleStepperT(float rpm, Direction direction) {
	stepper_command_t command = {
		.type = CommandType::SPINDLE,
		.rpm = rpm,
		.direction = direction,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(commandQueueT, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::stopStepperH(){
	stepper_command_t command = {
		.type = CommandType::STOP,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(commandQueueH, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::stopStepperT(){
	stepper_command_t command = {
		.type = CommandType::STOP,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(commandQueueT, &command, portMAX_DELAY)==  pdTRUE;
}

int64_t StepperHal::getStepsTraveledOfCurrentCommandH(){
	if(stepperCommandH->type == CommandType::STEPPER){
		int pulseCount = 0;
		pcnt_unit_get_count(pcntUnitH, &pulseCount); // NOTE: should handle complete case and be more precies than calculating from time
		return stepperCommandH->direction ? pulseCount : -pulseCount;
	}else if(stepperCommandH->type == CommandType::SPINDLE){
		int64_t pulseCount = (esp_timer_get_time() - stepperCommandH->timestamp) * stepperCommandH->rpm / 60'000'000;
		return stepperCommandT->direction ? pulseCount : -pulseCount;
	}
	return 0;
}

int64_t StepperHal::getStepsTraveledOfCurrentCommandT(){
	if(stepperCommandT->type == CommandType::STEPPER){
		int pulseCount = 0;
		pcnt_unit_get_count(pcntUnitT, &pulseCount);
		return stepperCommandT->direction ? pulseCount : -pulseCount;
	}else if(stepperCommandT->type == CommandType::SPINDLE){
		int64_t pulseCount = (esp_timer_get_time() - stepperCommandT->timestamp) * stepperCommandT->rpm / 60'000'000;
		return stepperCommandT->direction ? pulseCount : -pulseCount;
	}
	return 0;
}

int64_t StepperHal::getStepsTraveledOfPrevCommandH(){
	if(stepperCommandPrevH->synchronized)
		return 0;
	stepperCommandPrevH->synchronized = true;
	if(stepperCommandPrevH->type == CommandType::STEPPER){
		return stepperCommandPrevH->direction ? stepperCommandH->val.steps : -stepperCommandH->val.steps;
	}else if(stepperCommandPrevH->type == CommandType::SPINDLE){
		int64_t pulseCount = (stepperCommandPrevH->val.finishTime - stepperCommandPrevH->timestamp) * stepperCommandPrevH->rpm / 60'000'000;
		return stepperCommandPrevH->direction ? pulseCount : -pulseCount;
	}
	return 0;
}



int64_t StepperHal::getStepsTraveledOfPrevCommandT(){
	if(stepperCommandPrevH->synchronized)
		return 0;
	stepperCommandPrevH->synchronized = true;
	if(stepperCommandT->type == CommandType::STEPPER){
		int pulseCount = 0;
		pcnt_unit_get_count(pcntUnitT, &pulseCount);
		return stepperCommandT->direction ? pulseCount : -pulseCount;
	}else if(stepperCommandT->type == CommandType::SPINDLE){
		int64_t pulseCount = (esp_timer_get_time() - stepperCommandT->timestamp) * stepperCommandT->rpm / 60'000'000;
		return stepperCommandT->direction ? pulseCount : -pulseCount;
	}
	return 0;
}


