/*
 * stepper_hal.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_hal.h"

StepperHal steppers = StepperHal();
stepper_hal_struct_t* stepperHalH = new stepper_hal_struct_t();
stepper_hal_struct_t* stepperHalT = new stepper_hal_struct_t();

#include "driver/uart.h"

bool StepperHal::pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t* edata, void* user_ctx)
{
	// uart_write_bytes(UART_NUM_0, "PCNT reached\n", 13);
	if (unit == stepperHalH->pcntUnit) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(stepperHalH->pcntUnit, stepperHalH->stepperCommand->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_H);
		mcpwm_timer_start_stop(stepperHalH->timer, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to

	} else if (unit == stepperHalT->pcntUnit) {
		ESP_ERROR_CHECK(pcnt_unit_remove_watch_point(stepperHalT->pcntUnit, stepperHalT->stepperCommand->val.steps));
		xEventGroupSetBits(StepperHal::stepperEventGroup, STEPPER_COMPLETE_BIT_T);
		mcpwm_timer_start_stop(stepperHalT->timer, MCPWM_TIMER_START_STOP_FULL); // won't stop until we tell it to
	}
	return true;
}

void StepperHal::initStepperTasks()
{
	stepperHalH->stepperCompleteBit = STEPPER_COMPLETE_BIT_H;
	stepperHalT->stepperCompleteBit = STEPPER_COMPLETE_BIT_T;
	stepperHalH->stepperDirectionPin = (gpio_num_t)CONFIG_STEPPER_H_PIN_DIR;
	stepperHalT->stepperDirectionPin = (gpio_num_t)CONFIG_STEPPER_T_PIN_DIR;
	stepperHalH->commandQueue = xQueueCreate(CONFIG_STEPPER_HAL_QUEUE_SIZE, sizeof(stepper_hal_command_t));
	stepperHalT->commandQueue = xQueueCreate(CONFIG_STEPPER_HAL_QUEUE_SIZE, sizeof(stepper_hal_command_t));
	stepperHalH->stepperCommand = new stepper_hal_command_t(); // Don't really know why we need it, but
	stepperHalT->stepperCommand = new stepper_hal_command_t();
	stepperHalH->stepperCommandPrev = new stepper_hal_command_t();
	stepperHalT->stepperCommandPrev = new stepper_hal_command_t();

	xTaskCreate(stepperTask, "Stepper Task H", 2048, stepperHalH, 5,NULL);
	xTaskCreate(stepperTask, "Stepper Task T", 2048, stepperHalT, 5, NULL);
	ESP_LOGI(TAG, "Stepper tasks initialized");
}

void StepperHal::initMCPWN()
{
	// Configure MCPWM timer for stepper 1
	stepperEventGroup = xEventGroupCreate();

	mcpwm_timer_config_t timerH_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 100000, // 0.1 MHz resolution
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000, // Will adjust for RPM
	};
	mcpwm_new_timer(&timerH_config, &stepperHalH->timer);

	// Configure MCPWM timer for stepper 2
	mcpwm_timer_config_t timerT_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = 100000,
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP,
		.period_ticks = 1000,
	};
	mcpwm_new_timer(&timerT_config, &stepperHalT->timer);

	// Set up MCPWM operator for each stepper
	mcpwm_operator_config_t operatorConfig = {
		.group_id = 0,
		.intr_priority = 0, // as opposed to RMT this will possibly be bothered by other interrupts
	};
	// operator_config.group_id =0;
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &stepperHalH->oper));
	ESP_ERROR_CHECK(mcpwm_new_operator(&operatorConfig, &stepperHalT->oper));

	// Connect the operators to timers
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(stepperHalH->oper, stepperHalH->timer));
	ESP_ERROR_CHECK(mcpwm_operator_connect_timer(stepperHalT->oper, stepperHalT->timer));

	// Set up comparator and generator for each stepper
	mcpwm_comparator_config_t comparator_config = {
		.intr_priority = 0,
		.flags = {
				.update_cmp_on_tez = true,
		},
	};
	ESP_ERROR_CHECK(mcpwm_new_comparator(stepperHalH->oper, &comparator_config, &stepperHalH->comparator));
	ESP_ERROR_CHECK(mcpwm_new_comparator(stepperHalT->oper, &comparator_config, &stepperHalT->comparator));

	mcpwm_generator_config_t generator_config;
	generator_config.gen_gpio_num = CONFIG_STEPPER_H_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(stepperHalH->oper, &generator_config, &stepperHalH->generator));
	generator_config.gen_gpio_num = CONFIG_STEPPER_T_PIN_STEP;
	ESP_ERROR_CHECK(mcpwm_new_generator(stepperHalT->oper, &generator_config, &stepperHalT->generator));

	// Configure the generator actions - toggle on timer event
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(stepperHalH->generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));
	ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(stepperHalT->generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_TOGGLE)));

	ESP_ERROR_CHECK(mcpwm_timer_enable(stepperHalH->timer));
	ESP_ERROR_CHECK(mcpwm_timer_enable(stepperHalT->timer));

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
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &stepperHalT->pcntUnit));
	ESP_ERROR_CHECK(pcnt_new_unit(&unitConfig, &stepperHalH->pcntUnit));

	ESP_LOGI(TAG, "set glitch filter");
	pcnt_glitch_filter_config_t filterConfig = {
		.max_glitch_ns = 1000,
	};
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(stepperHalH->pcntUnit, &filterConfig));
	ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(stepperHalT->pcntUnit, &filterConfig));

	pcnt_chan_config_t channelConfig;
	channelConfig.edge_gpio_num = (gpio_num_t)CONFIG_STEPPER_H_PIN_SENSE;
	ESP_ERROR_CHECK(pcnt_new_channel(stepperHalH->pcntUnit, &channelConfig, &stepperHalH->pcntChan));
	channelConfig.edge_gpio_num = (gpio_num_t)CONFIG_STEPPER_T_PIN_SENSE;
	ESP_ERROR_CHECK(pcnt_new_channel(stepperHalT->pcntUnit, &channelConfig, &stepperHalT->pcntChan));

	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(stepperHalH->pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling
	ESP_ERROR_CHECK(pcnt_channel_set_edge_action(stepperHalT->pcntChan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD)); // increase on rising edge, hold on falling

	pcnt_event_callbacks_t cbs = {
		.on_reach = StepperHal::pcntOnReach,
	};

	stepperHalH->pcntQueue = xQueueCreate(10, sizeof(int));
	stepperHalT->pcntQueue = xQueueCreate(10, sizeof(int));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(stepperHalH->pcntUnit, &cbs, stepperHalH->pcntQueue));
	ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(stepperHalT->pcntUnit, &cbs, stepperHalT->pcntQueue));

	ESP_LOGI(TAG, "enable pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_enable(stepperHalH->pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_enable(stepperHalT->pcntUnit));
	ESP_LOGI(TAG, "clear pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_clear_count(stepperHalH->pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_clear_count(stepperHalT->pcntUnit));
	ESP_LOGI(TAG, "start pcnt unit");
	ESP_ERROR_CHECK(pcnt_unit_start(stepperHalH->pcntUnit));
	ESP_ERROR_CHECK(pcnt_unit_start(stepperHalT->pcntUnit));

	ESP_LOGI(TAG, "MCPWM initialized");
}

// NOTE: it would be possible to have two static structs, that would contain all the necessary information for the stepper
// that way a pointer to be passed to a task and we could have only one task for both steppers
void StepperHal::stepperTask(void* arg)
{
	stepper_hal_struct_t* stepperHal = (stepper_hal_struct_t*)arg;

	ESP_LOGI(TAG, "Starting stepper task %d", uxQueueMessagesWaiting(stepperHal->commandQueue));

	while (1) {
		if (xQueueReceive(stepperHal->commandQueue, stepperHal->stepperCommand, portMAX_DELAY)) {
			// if previous command was spindle, we are running a command that will change stepper movement we need to immediately set spindle regime end time
			if (stepperHal->stepperCommandPrev->type == CommandType::SPINDLE && stepperHal->stepperCommand->type < CommandType::SKIP)
				stepperHal->stepperCommandPrev->val.finishTime = esp_timer_get_time();

			uint32_t period_ticks = (uint32_t)(3'000'000 / CONFIG_STEPPER_H_STEP_COUNT / stepperHal->stepperCommand->rpm); // Convert to timer ticks (as we are toggling on timer event we need to double the RPM)

			gpio_set_level(stepperHal->stepperDirectionPin, stepperHal->stepperCommand->direction ? 1 : 0);

			// Reset and start pulse counter
			switch (stepperHal->stepperCommand->type) {
			case CommandType::STEPPER:
#ifdef CONFIG_STEPPER_DEBUG
				ESP_LOGI(TAG, "Stepper %s (STEPPER):\n\tperiod: %ld\n\tsteps: %ld", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T", period_ticks, stepperHal->stepperCommand->val.steps);
#endif
				stepperHal->stepperCommand->timestamp = esp_timer_get_time();
				stepperHal->stepperCommand->complete = false;
				ESP_ERROR_CHECK(pcnt_unit_add_watch_point(stepperHal->pcntUnit, stepperHal->stepperCommand->val.steps));
				ESP_LOGI(TAG, "PCNT added watch point, %ld", stepperHal->stepperCommand->val.steps);
				ESP_ERROR_CHECK(pcnt_unit_clear_count(stepperHal->pcntUnit));
				mcpwm_timer_set_period(stepperHal->timer, period_ticks);
				mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_NO_STOP);
				break;
			case CommandType::SPINDLE:
#ifdef CONFIG_STEPPER_DEBUG
				ESP_LOGI(TAG, "Stepper %s (SPINDLE):\n\tperiod: %ld", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T", period_ticks);
#endif
				stepperHal->stepperCommand->timestamp = esp_timer_get_time();
				stepperHal->stepperCommand->complete = false;
				xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
				mcpwm_timer_set_period(stepperHal->timer, period_ticks);
				mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_NO_STOP);
				vTaskDelay(CONFIG_STEPPER_MIN_SPINDLE_TIME / portTICK_PERIOD_MS); // NOTE: necessary delay to make sure information about previous command is read, if not present it would significantly complicate code
				break;
			case CommandType::SKIP:
#ifdef CONFIG_STEPPER_DEBUG
				ESP_LOGI(TAG, "Stepper %s (SKIP)", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T");
#endif
				stepperHal->stepperCommand->complete = false;
				if (stepperHal->stepperCommand->synchronized)
					vTaskDelay(1); // NOTE: this is a hack to make sure that both steppers ill wait for each other
				xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
				break;
			case CommandType::WAIT:
#ifdef CONFIG_STEPPER_DEBUG
				ESP_LOGI(TAG, "Stepper %s (WAIT):\n\ttime: %ld", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T", stepperHal->stepperCommand->val.time);
#endif
				stepperHal->stepperCommand->complete = false;
				vTaskDelay(stepperHal->stepperCommand->val.time / portTICK_PERIOD_MS);
				xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
				break;
			case CommandType::STOP:
#ifdef CONFIG_STEPPER_DEBUG
				ESP_LOGI(TAG, "Stepper %s (STOP)", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T");
#endif
				stepperHal->stepperCommand->complete = false;
				mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_STOP_FULL);
				xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
				break;
			}
			// TODO freeze execution until the bit is set
			EventBits_t result = xEventGroupWaitBits(
					stepperEventGroup,
					(stepperHal->stepperCommand->synchronized) ? (STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T) : stepperHal->stepperCompleteBit,
					pdTRUE, // TODO: need to verify this, should be fine according to https://forums.freertos.org/t/eventgroup-bit-clearing-when-multiple-tasks-wait-for-bit/7599
					pdTRUE,
					portMAX_DELAY);

			stepperHal->stepperCommand->complete = true;
#ifdef CONFIG_STEPPER_DEBUG
			ESP_LOGI(TAG, "Stepper %s completed", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T");
#endif

			if (stepperHal->stepperCommand->type < CommandType::STOP)
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
				.steps = steps >= 0 ? (uint32_t)steps : ((uint32_t)-steps),
		},
		.rpm = rpm,
		.direction = steps >= 0,
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
	return xQueueSend(stepperHalH->commandQueue, &command, portMAX_DELAY) == pdTRUE;
}


bool StepperHal::spindleStepper(stepper_hal_struct_t* stepperHal, float rpm, Direction direction)
{
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


bool StepperHal::carefullStop()
{
	clearQueue(stepperHalH);
	clearQueue(stepperHalT);
	return stopStepper(stepperHalH) && stopStepper(stepperHalT);
}


bool StepperHal::clearQueue(stepper_hal_struct_t* stepperHal)
{
	if (stepperHal->stepperCommand->type == CommandType::STEPPER) {
		pcnt_unit_remove_watch_point(stepperHal->pcntUnit, stepperHal->stepperCommand->val.steps);
	}
	return xQueueReset(stepperHal->commandQueue) == pdTRUE;
}


void StepperHal::stopNowStepper(stepper_hal_struct_t* stepperHal)
{ // immediately stop generator, clear the queue and remove watchpoint
	ESP_ERROR_CHECK(mcpwm_timer_start_stop(stepperHal->timer, MCPWM_TIMER_START_STOP_FULL));
	clearQueue(stepperHal);
	pcnt_unit_clear_count(stepperHal->pcntUnit);
	xEventGroupSetBits(StepperHal::stepperEventGroup, stepperHal->stepperCompleteBit);
}

int64_t StepperHal::getStepsTraveledOfCurrentCommand(stepper_hal_struct_t* stepperHal)
{
	if (stepperHal->stepperCommand->type == CommandType::STEPPER) {
		int pulseCount = 0;
		pcnt_unit_get_count(stepperHal->pcntUnit, &pulseCount); // NOTE: should handle complete case and be more precies than calculating from time
		return stepperHal->stepperCommand->direction ? pulseCount : -pulseCount;
	} else if (stepperHal->stepperCommand->type == CommandType::SPINDLE) {
		int64_t pulseCount = (esp_timer_get_time() - stepperHal->stepperCommand->timestamp) * stepperHal->stepperCommand->rpm / 60'000'000;
		return stepperHal->stepperCommand->direction ? pulseCount : -pulseCount;
	}
	return 0;
}

int64_t StepperHal::getStepsTraveledOfPrevCommand(stepper_hal_struct_t* stepperHal)
{
	if (stepperHal->stepperCommandPrev->synchronized)
		return 0;
	stepperHal->stepperCommandPrev->synchronized = true;
	if (stepperHal->stepperCommandPrev->type == CommandType::STEPPER) {
		return stepperHal->stepperCommandPrev->direction ? stepperHal->stepperCommand->val.steps : -stepperHal->stepperCommand->val.steps;
	} else if (stepperHal->stepperCommandPrev->type == CommandType::SPINDLE) {
		int64_t pulseCount = (stepperHal->stepperCommandPrev->val.finishTime - stepperHal->stepperCommandPrev->timestamp) * stepperHal->stepperCommandPrev->rpm / 60'000'000;
		return stepperHal->stepperCommandPrev->direction ? pulseCount : -pulseCount;
	}
	return 0;
}



uint8_t StepperHal::getQueueLength(stepper_hal_struct_t* stepperHal)
{
	return uxQueueMessagesWaiting(stepperHal->commandQueue);
}

bool StepperHal::peekQueue(stepper_hal_struct_t* stepperHal, stepper_hal_command_t* pointer)
{
	return xQueuePeek(stepperHal->commandQueue, pointer, portMAX_DELAY) == pdTRUE;
}

