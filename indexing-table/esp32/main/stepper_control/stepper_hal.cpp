/*
 * stepper_hal.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_hal.h"

StepperHal steppers = StepperHal();
stepper_hal_struct_t* stepperHalYaw = new stepper_hal_struct_t();
stepper_hal_struct_t* stepperHalPitch = new stepper_hal_struct_t();

#include "driver/uart.h"

bool StepperHal::pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t* edata, void* user_ctx)
{
	// uart_write_bytes(UART_NUM_0, "PCNT reached\n", 13);
	if (unit == stepperHalYaw->pcntUnit) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(stepperHalYaw->pcntUnit, stepperHalYaw->stepperCommand->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
		mcpwm_timer_start_stop(stepperHalYaw->timer, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
																																						 // pcnt_unit_stop(stepperHalYaw->pcntUnit);

	} else if (unit == stepperHalPitch->pcntUnit) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(stepperHalPitch->pcntUnit, stepperHalPitch->stepperCommand->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_T);
		mcpwm_timer_start_stop(stepperHalPitch->timer, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
																																						 // pcnt_unit_stop(stepperHalPitch->pcntUnit);
	}
	return true;
}

void StepperHal::initStepperTasks()
{
	stepperHalYaw->stepperCompleteBit = STEPPER_COMPLETE_BIT_H;
	stepperHalPitch->stepperCompleteBit = STEPPER_COMPLETE_BIT_T;
	stepperHalYaw->stepperDirectionPin = (gpio_num_t)CONFIG_STEPPER_Y_PIN_DIR;
	stepperHalPitch->stepperDirectionPin = (gpio_num_t)CONFIG_STEPPER_P_PIN_DIR;
	pinMode(stepperHalYaw->stepperDirectionPin, OUTPUT);
	pinMode(stepperHalPitch->stepperDirectionPin, OUTPUT);
	stepperHalYaw->stepCount = CONFIG_STEPPER_Y_STEP_COUNT;
	stepperHalPitch->stepCount = CONFIG_STEPPER_P_STEP_COUNT;

	stepperHalYaw->commandQueue = xQueueCreate(CONFIG_STEPPER_YAL_QUEUE_SIZE, sizeof(stepper_hal_command_t));
	stepperHalPitch->commandQueue = xQueueCreate(CONFIG_STEPPER_YAL_QUEUE_SIZE, sizeof(stepper_hal_command_t));
	stepperHalYaw->stepperCommand = new stepper_hal_command_t(); // Don't really know why we need it, but
	stepperHalPitch->stepperCommand = new stepper_hal_command_t();
	stepperHalYaw->stepperCommandPrev = new stepper_hal_command_t();
	stepperHalPitch->stepperCommandPrev = new stepper_hal_command_t();

	xTaskCreate(stepperTask, "Stepper Task H", 2048, stepperHalYaw, 5, NULL);
	xTaskCreate(stepperTask, "Stepper Task T", 2048, stepperHalPitch, 5, NULL);
	ESP_LOGI(TAG, "Stepper tasks initialized");
	xEventGroupClearBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
	xEventGroupClearBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_T);
}

void StepperHal::initTimers()
{
	stepperHalYaw->helperTimer = xTimerCreate(
			"StepperTimerH",
			1,									// Dummy period (overridden later)
			pdFALSE,						// One-shot
			(void*)stepperHalYaw, // Pass self as timer ID
			[](TimerHandle_t xTimer) {
				// Set THIS stepper's bit when timer expires
				xEventGroupSetBits(stepperEventGroup, STEPPER_COMPLETE_BIT_H);
			});

	stepperHalPitch->helperTimer = xTimerCreate(
			"StepperTimerT",
			1,									// Dummy period (overridden later)
			pdFALSE,						// One-shot
			(void*)stepperHalPitch, // Pass self as timer ID
			[](TimerHandle_t xTimer) {
				// Set THIS stepper's bit when timer expires
				xEventGroupSetBits(stepperEventGroup, STEPPER_COMPLETE_BIT_T);
			});
}

void StepperHal::initMCPWN()
{
	// Configure MCPWM timer for stepper 1
	stepperEventGroup = xEventGroupCreate();

	mcpwm_timer_config_t timerYawConfig = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 100000, // 0.1 MHz resolution
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000, // Will adjust for RPM
	};
	mcpwm_new_timer(&timerYawConfig, &stepperHalYaw->timer);

	// Configure MCPWM timer for stepper 2
	mcpwm_timer_config_t timerPitchConfig = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 100000,
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,
	};
	mcpwm_new_timer(&timerPitchConfig, &stepperHalPitch->timer);

	// Set up MCPWM operator for each stepper
	mcpwm_operator_config_t operatorConfig = {
		.group_id = 0,
		.intr_priority = 0, // as opposed to RMT this will possibly be bothered by other interrupts
	};
	// operator_config.group_id =0;
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &stepperHalYaw->oper));
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &stepperHalPitch->oper));

	// Connect the operators to timers
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(stepperHalYaw->oper, stepperHalYaw->timer));
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(stepperHalPitch->oper, stepperHalPitch->timer));

	// Set up comparator and generator for each stepper
	mcpwm_comparator_config_t comparator_config = {
		.intr_priority = 0,
		.flags = {
				.update_cmp_on_tez = true,
		},
	};
	ESP_ERROR_CHECK(mcpwm_new_comparator(stepperHalYaw->oper, &comparator_config, &stepperHalYaw->comparator));
	ESP_ERROR_CHECK(mcpwm_new_comparator(stepperHalPitch->oper, &comparator_config, &stepperHalPitch->comparator));

	mcpwm_generator_config_t generator_config;
	generator_config.gen_gpio_num = CONFIG_STEPPER_Y_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(stepperHalYaw->oper, &generator_config, &stepperHalYaw->generator));
	generator_config.gen_gpio_num = CONFIG_STEPPER_P_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(stepperHalPitch->oper, &generator_config, &stepperHalPitch->generator));

	// Configure the generator actions - toggle on timer event
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(stepperHalYaw->generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(stepperHalPitch->generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));

	ESP_ERROR_CHECK(mcpwm_timer_enable(stepperHalYaw->timer));
	ESP_ERROR_CHECK(mcpwm_timer_enable(stepperHalPitch->timer));

	ESP_LOGI(TAG, "MCPWM initialized");
}

void StepperHal::initPCNT()
{
	ESP_LOGI(TAG, "Initializing PCNT for pulse counters");
	pcnt_unit_config_t unitConfig = {
		.low_limit = -1,
		.high_limit = 32767,
		.intr_priority = 0,
	};
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &stepperHalPitch->pcntUnit));
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &stepperHalYaw->pcntUnit));

	ESP_LOGI(TAG, "set glitch filter");
	pcnt_glitch_filter_config_t filterConfig = {
		.max_glitch_ns = 1000,
	};
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(stepperHalYaw->pcntUnit, &filterConfig));
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(stepperHalPitch->pcntUnit, &filterConfig));

	pcnt_chan_config_t channelConfig;
	channelConfig.edge_gpio_num = (gpio_num_t)CONFIG_STEPPER_Y_PIN_SENSE;
	ESP_ERROR_CHECK(pcnt_new_channel(stepperHalYaw->pcntUnit, &channelConfig, &stepperHalYaw->pcntChan));
	channelConfig.edge_gpio_num = (gpio_num_t)CONFIG_STEPPER_P_PIN_SENSE;
	ESP_ERROR_CHECK(pcnt_new_channel(stepperHalPitch->pcntUnit, &channelConfig, &stepperHalPitch->pcntChan));

	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(stepperHalYaw->pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling
	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(stepperHalPitch->pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling

	pcnt_event_callbacks_t cbs = {
		.on_reach = StepperHal::pcntOnReach,
	};

	stepperHalYaw->pcntQueue = xQueueCreate(10, sizeof(int));
	stepperHalPitch->pcntQueue = xQueueCreate(10, sizeof(int));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(stepperHalYaw->pcntUnit, &cbs, stepperHalYaw->pcntQueue));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(stepperHalPitch->pcntUnit, &cbs, stepperHalPitch->pcntQueue));

	ESP_LOGI(TAG, "enable pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_enable(stepperHalYaw->pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_enable(stepperHalPitch->pcntUnit));
	ESP_LOGI(TAG, "clear pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_clear_count(stepperHalYaw->pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_clear_count(stepperHalPitch->pcntUnit));
	// ESP_LOGI(TAG, "start pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_start(stepperHalYaw->pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_start(stepperHalPitch->pcntUnit));

	ESP_LOGI(TAG, "MCPWM initialized");
}

void StepperHal::stepperTask(void* arg)
{
	stepper_hal_struct_t* stepperHal = (stepper_hal_struct_t*)arg;
	const char* stepperSign = stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "Y" : "P";
	while (1) {
		if (xQueueReceive(stepperHal->commandQueue, stepperHal->stepperCommand, portMAX_DELAY)) {
			// if previous command was spindle, we are running a command that will change stepper movement we need to immediately set spindle regime end time
			if (stepperHal->stepperCommandPrev->type == CommandType::SPINDLE && stepperHal->stepperCommand->type < CommandType::SKIP){
				stepperHal->stepperCommandPrev->val.finishTime = esp_timer_get_time();
				stepperHal->stepperCommandPrev->complete = true;
			}


			// Convert to timer ticks (as we are toggling on timer event we need to double the RPM)
			// ESP is slightly faster than we would like need to set period slightly higher
			uint32_t period_ticks = (uint32_t)(3'007'760 / stepperHal->stepCount / stepperHal->stepperCommand->rpm);

			gpio_set_level(stepperHal->stepperDirectionPin, !stepperHal->stepperCommand->direction); // NOTE I have wired both steppers in reverse, so I just flip it here
			EventBits_t t = xEventGroupGetBits(StepperHal::stepperEventGroup);


#ifdef CONFIG_HAL_DEBUG
			// with synchronized commands neither of these if's should trigger

			if (stepperHal->stepperCommand->synchronized && t & STEPPER_COMPLETE_BIT_H && t & STEPPER_COMPLETE_BIT_T)
				ESP_LOGE(TAG, "stepperTask | %s completed Y and P", stepperSign);
			else if (stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H && t & STEPPER_COMPLETE_BIT_H)
				ESP_LOGE(TAG, "stepperTask | %s completed Y", stepperSign);
			else if (stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_T && t & STEPPER_COMPLETE_BIT_T)
				ESP_LOGE(TAG, "stepperTask | %s completed P", stepperSign);
#endif

			// Reset and start pulse counter
			switch (stepperHal->stepperCommand->type) {
			case CommandType::STEPPER: {
#ifdef CONFIG_HAL_DEBUG
				ESP_LOGI(TAG, "stepperTask | %s (STEPPER), direction %d, period: %ld, steps: %ld", stepperSign, stepperHal->stepperCommand->direction, period_ticks, stepperHal->stepperCommand->val.steps);
#endif
				stepperHal->stepperCommand->complete = false;
				ESP_ERROR_CHECK(pcnt_unit_clear_count(stepperHal->pcntUnit));
				ESP_ERROR_CHECK(pcnt_unit_add_watch_point(stepperHal->pcntUnit, stepperHal->stepperCommand->val.steps));
				ESP_ERROR_CHECK(mcpwm_timer_set_period(stepperHal->timer, period_ticks));
				mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_NO_STOP);
				stepperHal->stepperCommand->timestamp = esp_timer_get_time();
				break;
			}
			case CommandType::SPINDLE: {
#ifdef CONFIG_HAL_DEBUG
				ESP_LOGI(TAG, "stepperTask | %s (SPINDLE), direction %d, period: %ld", stepperSign, stepperHal->stepperCommand->direction, period_ticks);
#endif
				stepperHal->stepperCommand->timestamp = esp_timer_get_time();
				stepperHal->stepperCommand->complete = false;
				mcpwm_timer_set_period(stepperHal->timer, period_ticks);
				mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_NO_STOP);
				xTimerChangePeriod(stepperHal->helperTimer, CONFIG_STEPPER_MIN_SPINDLE_TIME / portTICK_PERIOD_MS, portMAX_DELAY); // delay reading next command so that application layer has enough time to process previous command
				xTimerStart(stepperHal->helperTimer, 0);
				break;
			}
			case CommandType::SKIP: {
#ifdef CONFIG_HAL_DEBUG
				ESP_LOGI(TAG, "stepperTask | %s (SKIP)", stepperSign);
#endif
				stepperHal->stepperCommand->complete = false;
				if (stepperHal->stepperCommand->synchronized) {
					xTimerChangePeriod(stepperHal->helperTimer, 1, portMAX_DELAY);
					xTimerStart(stepperHal->helperTimer, 0);
				} else
					xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
				break;
			}
			case CommandType::WAIT: {
#ifdef CONFIG_HAL_DEBUG
				ESP_LOGI(TAG, "stepperTask | %s (WAIT), time: %ld", stepperSign, stepperHal->stepperCommand->val.time);
#endif
				stepperHal->stepperCommand->complete = false;
				uint32_t time = stepperHal->stepperCommand->val.time / portTICK_PERIOD_MS;
				xTimerChangePeriod(stepperHal->helperTimer, time >= 1 ? time : 1, portMAX_DELAY);
				xTimerStart(stepperHal->helperTimer, 0);
				break;
			}
			case CommandType::STOP: {
#ifdef CONFIG_HAL_DEBUG
				ESP_LOGI(TAG, "stepperTask | %s (STOP)", stepperSign);
#endif
				stepperHal->stepperCommand->complete = false;
				mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_STOP_FULL);
				if (stepperHal->stepperCommand->synchronized) {
					xTimerChangePeriod(stepperHal->helperTimer, 1, portMAX_DELAY);
					xTimerStart(stepperHal->helperTimer, 0);
				} else
					xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
				break;
			}
			}
			EventBits_t result = xEventGroupWaitBits(
					stepperEventGroup,
					(stepperHal->stepperCommand->synchronized) ? (STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T) : stepperHal->stepperCompleteBit,
					pdTRUE, // should be fine here (according to https://forums.freertos.org/t/eventgroup-bit-clearing-when-multiple-tasks-wait-for-bit/7599)
					pdTRUE,
					portMAX_DELAY);

			if(stepperHal->stepperCommand->type != CommandType::SPINDLE)
				stepperHal->stepperCommand->complete = true;

#ifdef CONFIG_HAL_DEBUG
			ESP_LOGI(TAG, "stepperTask | %s completed", stepperSign);
#endif

			if (stepperHal->stepperCommand->type < CommandType::STOP) // SKIP, WAIT, STOP commands don't affect position so we can simply drop them
				memcpy(stepperHal->stepperCommandPrev, stepperHal->stepperCommand, sizeof(stepper_hal_command_t));
			stepperHal->stepperCommandPrev->synchronized = false;
		}
	}
}

bool StepperHal::stepStepper(stepper_hal_struct_t* stepperHal, int16_t steps, float rpm, bool synchronized)
{
	stepper_hal_command_t command = {
		.type = steps == 0 ? CommandType::SKIP : CommandType::STEPPER,
		.val = {
				.steps = steps >= 0 ? (uint16_t)steps : ((uint16_t)-steps),
		},
		.rpm = rpm,
		.direction = steps >= 0 ? Direction::FORWARD : Direction::BACKWARD,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(stepperHal->commandQueue, &command, portMAX_DELAY) == pdTRUE;
}

bool StepperHal::waitStepper(stepper_hal_struct_t* stepperHal, uint32_t time, bool synchronized)
{
	stepper_hal_command_t command = {
		.type = CommandType::WAIT,
		.val = {
				.time = time,
		},
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(stepperHal->commandQueue, &command, portMAX_DELAY) == pdTRUE;
}

bool StepperHal::spindleStepper(stepper_hal_struct_t* stepperHal, float rpm, Direction direction)
{
	// ESP_LOGI(TAG, "Spindle stepper %s, rpm: %f", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T", rpm);
	stepper_hal_command_t command = {
		.type = CommandType::SPINDLE,
		.rpm = rpm,
		.direction = direction,
		.complete = false,
		.synchronized = false,
	};
	return xQueueSend(stepperHal->commandQueue, &command, portMAX_DELAY) == pdTRUE;
}

bool StepperHal::skipStepper(stepper_hal_struct_t* stepperHal, bool synchronized)
{
	stepper_hal_command_t command = {
		.type = CommandType::SKIP,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(stepperHal->commandQueue, &command, portMAX_DELAY) == pdTRUE;
}

bool StepperHal::stopStepper(stepper_hal_struct_t* stepperHal, bool synchronized)
{
	stepper_hal_command_t command = {
		.type = CommandType::STOP,
		.complete = false,
		.synchronized = synchronized,
	};
	return xQueueSend(stepperHal->commandQueue, &command, portMAX_DELAY) == pdTRUE;
}

bool StepperHal::stopNowStepper(stepper_hal_struct_t* stepperHal)
{

	bool toRet = xQueueReset(stepperHal->commandQueue) == pdTRUE;
	if (stepperHal->stepperCommand->complete) // there is no cleanup needed to be made
		return toRet;

	mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_STOP_FULL);

	if (stepperHal->stepperCommand->type == CommandType::STEPPER) {
		int pulseCount = 0;
		pcnt_unit_get_count(stepperHal->pcntUnit, &pulseCount);
		pcnt_unit_remove_watch_point(stepperHal->pcntUnit, stepperHal->stepperCommand->val.steps);
		stepperHal->stepperCommand->val.steps = pulseCount;
	}
	pcnt_unit_clear_count(stepperHal->pcntUnit);
	return toRet;
}

int64_t StepperHal::getStepsTraveledOfCurrentCommand(stepper_hal_struct_t* stepperHal)
{
	if(stepperHal->stepperCommand->complete)
		return 0;

	if (stepperHal->stepperCommand->type == CommandType::STEPPER) {
		int pulseCount = 0;
		pcnt_unit_get_count(stepperHal->pcntUnit, &pulseCount); // NOTE: should handle complete case and be more precies than calculating from time
		return stepperHal->stepperCommand->direction ? -pulseCount : pulseCount;
	} else if (stepperHal->stepperCommand->type == CommandType::SPINDLE) { // case for STEPPER_MIN_SPINDLE_TIME where stepper command is not yet copied into previous one
		int64_t pulseCount = (esp_timer_get_time() - stepperHal->stepperCommand->timestamp) * stepperHal->stepperCommand->rpm / 60'000'000 * stepperHal->stepCount;
		return stepperHal->stepperCommand->direction ? -pulseCount : pulseCount;
	} else if (stepperHal->stepperCommandPrev->type == CommandType::SPINDLE && !stepperHal->stepperCommandPrev->complete) { // case for rest of the spindle movement (stepperCommand will usuals now be WAIT)
		int64_t pulseCount = (esp_timer_get_time() - stepperHal->stepperCommandPrev->timestamp) * stepperHal->stepperCommandPrev->rpm / 60'000'000 * stepperHal->stepCount;
		return stepperHal->stepperCommandPrev->direction ? -pulseCount : pulseCount;
	}
	return 0;
}

int64_t StepperHal::getStepsTraveledOfPrevCommand(stepper_hal_struct_t* stepperHal)
{
	if (stepperHal->stepperCommandPrev->synchronized)
		return 0;
	if (stepperHal->stepperCommandPrev->type == CommandType::STEPPER) {
		stepperHal->stepperCommandPrev->synchronized = true;
		return stepperHal->stepperCommandPrev->direction ? -stepperHal->stepperCommandPrev->val.steps : stepperHal->stepperCommandPrev->val.steps;
	} else if (stepperHal->stepperCommandPrev->type == CommandType::SPINDLE && stepperHal->stepperCommandPrev->complete) {
		stepperHal->stepperCommandPrev->synchronized = true;
		int64_t pulseCount = (stepperHal->stepperCommandPrev->val.finishTime - stepperHal->stepperCommandPrev->timestamp) * stepperHal->stepperCommandPrev->rpm / 60'000'000 * stepperHal->stepCount;
		return stepperHal->stepperCommandPrev->direction ? -pulseCount : +pulseCount;
	}
	return 0;
}

uint8_t StepperHal::getQueueLength(stepper_hal_struct_t* stepperHal)
{
	return uxQueueMessagesWaiting(stepperHal->commandQueue);
}

stepper_hal_command_t* StepperHal::peekQueue(stepper_hal_struct_t* stepperHal)
{
	stepper_hal_command_t* pointer = nullptr;
	if (xQueuePeek(stepperHal->commandQueue, pointer, portMAX_DELAY) == pdTRUE)
		return pointer;
	else
		return nullptr;
}
