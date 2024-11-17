/*
 * stepper_hal.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_hal.h"

StepperHal steppers = StepperHal();

stepper_hal_variables_t StepperHal::varsHalH;
stepper_hal_variables_t StepperHal::varsHalT;


bool StepperHal::pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx){
	if(unit == varsHalH.pcntUnit) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(varsHalH.pcntUnit, varsHalH.stepperCommand->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
		mcpwm_timer_start_stop(varsHalH.timer, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to

	} else if(unit == varsHalT.pcntUnit) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(varsHalT.pcntUnit, varsHalT.stepperCommand->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_T);
		mcpwm_timer_start_stop(varsHalT.timer, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
	}
	return true;
}

void StepperHal::initStepperTasks(){
	varsHalH.stepperCompleteBit = STEPPER_COMPLETE_BIT_H;
	varsHalT.stepperCompleteBit = STEPPER_COMPLETE_BIT_T;
	varsHalH.commandQueue = xQueueCreate(CONFIG_STEPPER_HAL_QUEUE_SIZE, sizeof(stepper_hal_command_t));
	varsHalT.commandQueue = xQueueCreate(CONFIG_STEPPER_HAL_QUEUE_SIZE, sizeof(stepper_hal_command_t));
	xTaskCreate(stepperTask, "Stepper Task T", 2048, &varsHalH, 5, NULL);
	xTaskCreate(stepperTask, "Stepper Task H", 2048, &varsHalT, 5, NULL);

}

void StepperHal::initMCPWN() {
	// Configure MCPWM timer for stepper 1
	stepperEventGroup = xEventGroupCreate();

	mcpwm_timer_config_t timerH_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 1000000,  // 1 MHz resolution
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,      // Will adjust for RPM
	};
	mcpwm_new_timer(&timerH_config, &varsHalH.timer);

	// Configure MCPWM timer for stepper 2
	mcpwm_timer_config_t timerT_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 1000000,
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,
	};
	mcpwm_new_timer(&timerT_config, &varsHalT.timer);

	// Set up MCPWM operator for each stepper
	mcpwm_operator_config_t operatorConfig = {
		.group_id = 0,
		.intr_priority = 0, // as opposed to RMT this will possibly be bothered by other interrupts
	};
	// operator_config.group_id =0;
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &varsHalH.oper));
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &varsHalT.oper));

	// Connect the operators to timers
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(varsHalH.oper, varsHalH.timer));
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(varsHalT.oper, varsHalT.timer));

	// Set up comparator and generator for each stepper
	mcpwm_comparator_config_t comparator_config = {
		.intr_priority = 0,
		.flags = {
			.update_cmp_on_tez = true,
		},
	};
	ESP_ERROR_CHECK(mcpwm_new_comparator(varsHalH.oper, &comparator_config, &varsHalH.comparator));
	ESP_ERROR_CHECK(mcpwm_new_comparator(varsHalT.oper, &comparator_config, &varsHalT.comparator));

	mcpwm_generator_config_t generator_config;
	generator_config.gen_gpio_num = CONFIG_STEPPER_H_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(varsHalH.oper, &generator_config, &varsHalH.generator));
	generator_config.gen_gpio_num = CONFIG_STEPPER_T_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(varsHalT.oper, &generator_config, &varsHalT.generator));


	// Configure the generator actions - toggle on timer event
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(varsHalH.generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(varsHalT.generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));

	ESP_ERROR_CHECK(mcpwm_timer_enable(varsHalH.timer));
	ESP_ERROR_CHECK(mcpwm_timer_enable(varsHalT.timer));
}

void StepperHal::initPCNT(){
	const char *TAG = "PCNT";
	ESP_LOGI(TAG, "Initializing PCNT for pulse counters");
	pcnt_unit_config_t unitConfig = {
		.low_limit = -1,
		.high_limit = 32767,
		.intr_priority = 0,
	};
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &varsHalT.pcntUnit));
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &varsHalH.pcntUnit));


	ESP_LOGI(TAG, "set glitch filter");
	pcnt_glitch_filter_config_t filterConfig = {
		.max_glitch_ns = 1000,
	};
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(varsHalH.pcntUnit, &filterConfig));
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(varsHalT.pcntUnit, &filterConfig));

	pcnt_chan_config_t channelConfig;
	channelConfig.edge_gpio_num = (gpio_num_t) CONFIG_STEPPER_H_PIN_STEP;
	ESP_ERROR_CHECK(pcnt_new_channel(varsHalH.pcntUnit, &channelConfig, &varsHalH.pcntChan));
	channelConfig.edge_gpio_num = (gpio_num_t) CONFIG_STEPPER_T_PIN_STEP;
	ESP_ERROR_CHECK(pcnt_new_channel(varsHalT.pcntUnit, &channelConfig, &varsHalT.pcntChan));


	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(varsHalH.pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling
	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(varsHalT.pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling

	pcnt_event_callbacks_t cbs = {
		.on_reach = StepperHal::pcntOnReach,
	};

	varsHalH.pcntQueue = xQueueCreate(10, sizeof(int));
	varsHalT.pcntQueue = xQueueCreate(10, sizeof(int));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(varsHalH.pcntUnit, &cbs, varsHalH.pcntQueue));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(varsHalT.pcntUnit, &cbs, varsHalT.pcntQueue));

	ESP_LOGI(TAG, "enable pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_enable(varsHalH.pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_enable(varsHalT.pcntUnit));
	ESP_LOGI(TAG, "clear pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_clear_count(varsHalH.pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_clear_count(varsHalT.pcntUnit));
	ESP_LOGI(TAG, "start pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_start(varsHalH.pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_start(varsHalT.pcntUnit));
}

// NOTE: it would be possible to have two static structs, that would contain all the necessary information for the stepper
// that way a pointer to be passed to a task and we could have only one task for both steppers
void StepperHal::stepperTask(void *arg) {
	stepper_hal_variables_t* varsHal = (stepper_hal_variables_t*) arg;

	while (1) {
		if (xQueueReceive(varsHal->commandQueue, varsHal->stepperCommand, portMAX_DELAY)) {
			// if previous command was spindle, we are running a command that will change stepper movement we need to immediately set spindle regime end time
			if(varsHal->stepperCommandPrev->type == CommandType::SPINDLE && varsHal->stepperCommand->type < CommandType::SKIP)
				varsHal->stepperCommandPrev->val.finishTime = esp_timer_get_time();


			ESP_LOGI("Stepper 1", "Received command for stepper 1");
			uint32_t period_ticks = (uint32_t)(60'000'000/CONFIG_STEPPER_H_STEP_COUNT/varsHal->stepperCommand->rpm); // Convert to timer ticks (as we are toggling on timer event we need to double the RPM)
			gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_DIR, varsHal->stepperCommand->direction ? 1 : 0);

			// Reset and start pulse counter
			switch(varsHal->stepperCommand->type){
				case CommandType::STEPPER:
					varsHal->stepperCommand->timestamp = esp_timer_get_time();
					varsHal->stepperCommand->complete = false;
					ESP_ERROR_CHECK(pcnt_unit_add_watch_point(varsHal->pcntUnit, varsHal->stepperCommand->val.steps));
					ESP_ERROR_CHECK(pcnt_unit_clear_count(varsHal->pcntUnit));
					mcpwm_timer_set_period(varsHalH.timer, period_ticks);
					mcpwm_timer_start_stop(varsHalH.timer, MCPWM_TIMER_START_NO_STOP);
					break;
				case CommandType::SPINDLE:
					varsHal->stepperCommand->timestamp = esp_timer_get_time();
					varsHal->stepperCommand->complete = false;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					mcpwm_timer_set_period(varsHalH.timer, period_ticks);
					mcpwm_timer_start_stop(varsHalH.timer, MCPWM_TIMER_START_NO_STOP);
					vTaskDelay(CONFIG_STEPPER_MIN_SPINDLE_TIME/portTICK_PERIOD_MS); // NOTE: necessary delay to make sure information about previous command is read, if not present it would significantly complicate code
					break;
				case CommandType::SKIP:
					varsHal->stepperCommand->complete = false;
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;
				case CommandType::WAIT:
					varsHal->stepperCommand->complete = false;
					vTaskDelay(varsHal->stepperCommand->val.time / portTICK_PERIOD_MS);
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;
				case CommandType::STOP:
					varsHal->stepperCommand->complete = false;
					mcpwm_timer_start_stop(varsHalH.timer, MCPWM_TIMER_START_STOP_FULL);
					xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
					break;

			}
			// TODO freeze execution until the bit is set
			EventBits_t result = xEventGroupWaitBits(
					stepperEventGroup,
					(varsHal->stepperCommand->synchronized) ? STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T : varsHal->stepperCompleteBit,
					pdTRUE, // TODO: need to verify this, should be fine according to https://forums.freertos.org/t/eventgroup-bit-clearing-when-multiple-tasks-wait-for-bit/7599
					pdTRUE,
					portMAX_DELAY
					);
			varsHal->stepperCommand->complete = true;

			if(varsHal->stepperCommand->type < CommandType::STOP)
				memcpy(varsHal->stepperCommandPrev, varsHal->stepperCommand, sizeof(stepper_hal_command_t));
			varsHal->stepperCommandPrev->synchronized = false;
		}
	}
}

bool StepperHal::stepStepperH(int16_t steps, float rpm, bool synchronized) {
	stepper_hal_command_t command = {
		.type = steps == 0 ? CommandType::SKIP : CommandType::STEPPER,
		.val = {
			.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		},
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalH.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

// Function to set a command for stepper 2
bool StepperHal::stepStepperT(int16_t steps, float rpm, bool synchronized) {

	stepper_hal_command_t command = {
		.type = steps == 0 ? CommandType::SKIP : CommandType::STEPPER,
		.val = {
			.steps = steps >= 0 ? (uint32_t) steps : ((uint32_t ) -steps),
		},
		.rpm = rpm,
		.direction = steps >= 0,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalT.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::waitStepperH(uint32_t time, bool synchronized) {
	stepper_hal_command_t command = {
		.type = CommandType::WAIT,
		.val = {.time = time,},
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalH.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::waitStepperT(uint32_t time, bool synchronized) {
	stepper_hal_command_t command = {
		.type = CommandType::WAIT,
		.val= {.time = time,},
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalT.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::spindleStepperH(float rpm, Direction direction) {
	stepper_hal_command_t command = {
		.type = CommandType::SPINDLE,
		.rpm = rpm,
		.direction = direction,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(varsHalH.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::spindleStepperT(float rpm, Direction direction) {
	stepper_hal_command_t command = {
		.type = CommandType::SPINDLE,
		.rpm = rpm,
		.direction = direction,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(varsHalT.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::skipStepperH(bool synchronized){
	stepper_hal_command_t command = {
		.type = CommandType::SKIP,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalH.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::skipStepperT(bool synchronized){
	stepper_hal_command_t command = {
		.type = CommandType::SKIP,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalT.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}


bool StepperHal::stopStepperH(bool synchronized){
	stepper_hal_command_t command = {
		.type = CommandType::STOP,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalH.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

bool StepperHal::stopStepperT(bool synchronized){
	stepper_hal_command_t command = {
		.type = CommandType::STOP,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(varsHalT.commandQueue, &command, portMAX_DELAY)==  pdTRUE;
}

void StepperHal::stopNowStepperH(){ // impediately stop generator, clear the queue and remove watchpoint
	ESP_ERROR_CHECK(mcpwm_timer_start_stop(varsHalH.timer, MCPWM_TIMER_START_STOP_FULL));
	if(varsHalH.stepperCommand->type == CommandType::STEPPER)
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(varsHalH.pcntUnit, varsHalH.stepperCommand->val.steps));
}

void StepperHal::stopNowStepperT(){ // impediately stop generator, clear the queue and remove watchpoint
	ESP_ERROR_CHECK(mcpwm_timer_start_stop(varsHalT.timer, MCPWM_TIMER_START_STOP_FULL));
	if(varsHalT.stepperCommand->type == CommandType::STEPPER)
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(varsHalT.pcntUnit, varsHalT.stepperCommand->val.steps));
}

int64_t StepperHal::getStepsTraveledOfCurrentCommandH(){
	if(varsHalH.stepperCommand->type == CommandType::STEPPER){
		int pulseCount = 0;
		pcnt_unit_get_count(varsHalH.pcntUnit, &pulseCount); // NOTE: should handle complete case and be more precies than calculating from time
		return varsHalH.stepperCommand->direction ? pulseCount : -pulseCount;
	}else if(varsHalH.stepperCommand->type == CommandType::SPINDLE){
		int64_t pulseCount = (esp_timer_get_time() - varsHalH.stepperCommand->timestamp) * varsHalH.stepperCommand->rpm / 60'000'000;
		return varsHalT.stepperCommand->direction ? pulseCount : -pulseCount;
	}
	return 0;
}

int64_t StepperHal::getStepsTraveledOfCurrentCommandT(){
	if(varsHalT.stepperCommand->type == CommandType::STEPPER){
		int pulseCount = 0;
		pcnt_unit_get_count(varsHalT.pcntUnit, &pulseCount);
		return varsHalT.stepperCommand->direction ? pulseCount : -pulseCount;
	}else if(varsHalT.stepperCommand->type == CommandType::SPINDLE){
		int64_t pulseCount = (esp_timer_get_time() - varsHalT.stepperCommand->timestamp) * varsHalT.stepperCommand->rpm / 60'000'000;
		return varsHalT.stepperCommand->direction ? pulseCount : -pulseCount;
	}
	return 0;
}

int64_t StepperHal::getStepsTraveledOfPrevCommandH(){
	if(varsHalH.stepperCommandPrev->synchronized)
		return 0;
	varsHalH.stepperCommandPrev->synchronized = true;
	if(varsHalH.stepperCommandPrev->type == CommandType::STEPPER){
		return varsHalH.stepperCommandPrev->direction ? varsHalH.stepperCommand->val.steps : -varsHalH.stepperCommand->val.steps;
	}else if(varsHalH.stepperCommandPrev->type == CommandType::SPINDLE){
		int64_t pulseCount = (varsHalH.stepperCommandPrev->val.finishTime - varsHalH.stepperCommandPrev->timestamp) * varsHalH.stepperCommandPrev->rpm / 60'000'000;
		return varsHalH.stepperCommandPrev->direction ? pulseCount : -pulseCount;
	}
	return 0;
}



int64_t StepperHal::getStepsTraveledOfPrevCommandT(){
	if(varsHalH.stepperCommandPrev->synchronized)
		return 0;
	varsHalH.stepperCommandPrev->synchronized = true;
	if(varsHalT.stepperCommand->type == CommandType::STEPPER){
		int pulseCount = 0;
		pcnt_unit_get_count(varsHalT.pcntUnit, &pulseCount);
		return varsHalT.stepperCommand->direction ? pulseCount : -pulseCount;
	}else if(varsHalT.stepperCommand->type == CommandType::SPINDLE){
		int64_t pulseCount = (esp_timer_get_time() - varsHalT.stepperCommand->timestamp) * varsHalT.stepperCommand->rpm / 60'000'000;
		return varsHalT.stepperCommand->direction ? pulseCount : -pulseCount;
	}
	return 0;
}


