/*
 * motor_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "motor_control.h"

MotorControl* MotorControl::instance = nullptr;

void IRAM_ATTR MotorControl::timerISRHandleHori(void *arg) {
	horiPulseCount++;
	if (horiPulseCount >= horiCurrentCommand.steps) { // NOTE: if we are in spindle mode we will never check this condition, xEventGroupSetBits will be always on
		mcpwm_stop(STEPPER1_MCPWM_UNIT, STEPPER1_MCPWM_TIMER);
		horiCurrentCommand.complete = true;
		horiPulseCount = 0;
		xEventGroupSetBits(stepperCompleteEventGroup, STEPPER_COMPLETE_BIT_1);
	}
	timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
}

void IRAM_ATTR MotorControl::timerISRHandleTilt(void *arg) {
	tiltPulseCount++;
	if (tiltPulseCount >= tiltCurrentCommand.steps) {
		mcpwm_stop(STEPPER2_MCPWM_UNIT, STEPPER2_MCPWM_TIMER);
		tiltCurrentCommand.complete = true;
		tiltPulseCount = 0;
		xEventGroupSetBits(stepperCompleteEventGroup, STEPPER_COMPLETE_BIT_2);
	}
	timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_1);
	// TIMERG0.int_clr_timers.t1 = 1;
}

void MotorControl::mcpwmInit(){
	mcpwm_gpio_init(STEPPER1_MCPWM_UNIT, MCPWM0A, CONFIG_MOTR_H_PIN_STEP);
	mcpwm_gpio_init(STEPPER2_MCPWM_UNIT, MCPWM1A, CONFIG_MOTR_T_PIN_STEP);

	mcpwm_config_t pwm_config = {
		.frequency = 1000, // Initial frequency, will be adjusted for RPM
		.cmpr_a = 50.0,
		.duty_mode = MCPWM_DUTY_MODE_0,
		.counter_mode = MCPWM_UP_COUNTER,
	};
	mcpwm_init(STEPPER1_MCPWM_UNIT, STEPPER1_MCPWM_TIMER, &pwm_config);
	mcpwm_init(STEPPER2_MCPWM_UNIT, STEPPER2_MCPWM_TIMER, &pwm_config);
}

void MotorControl::timerInit(){
	timer_config_t config = {
		.alarm_en = TIMER_ALARM_EN,
		.counter_en = TIMER_PAUSE,
		.counter_dir = TIMER_COUNT_UP,
		.auto_reload = TIMER_AUTORELOAD_EN,
		.divider = 80,
	};
	timer_init(TIMER_GROUP_0, TIMER_0, &config);
	timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 1000);  // 1 ms interrupt
	timer_enable_intr(TIMER_GROUP_0, TIMER_0);
	timer_isr_register(TIMER_GROUP_0, TIMER_0, timerISRHandleHori, NULL, ESP_INTR_FLAG_IRAM, NULL);

	timer_init(TIMER_GROUP_0, TIMER_1, &config);
	timer_set_alarm_value(TIMER_GROUP_0, TIMER_1, 1000);  // 1 ms interrupt
	timer_enable_intr(TIMER_GROUP_0, TIMER_1);
	timer_isr_register(TIMER_GROUP_0, TIMER_1, timerISRHandleTilt, NULL, ESP_INTR_FLAG_IRAM, NULL);
}

void MotorControl::horiTask(void *arg) {
	while (1) {
		if (xQueueReceive(horiCommandQueue, &horiCurrentCommand, portMAX_DELAY)) {
			float pulse_frequency = (horiCurrentCommand.rpm * 200) / 60.0;
			horiCurrentCommand.complete = false;
			horiPulseCount = 0;

			gpio_set_level((gpio_num_t) CONFIG_MOTR_H_PIN_DIR, horiCurrentCommand.direction ? 1 : 0);
			mcpwm_set_frequency(STEPPER1_MCPWM_UNIT, STEPPER1_MCPWM_TIMER, (int)pulse_frequency);
			mcpwm_start(STEPPER1_MCPWM_UNIT, STEPPER1_MCPWM_TIMER);
			timer_start(TIMER_GROUP_0, TIMER_0);  // Start counting pulses
		}
	}
}

// Task to process commands for stepper 2
void MotorControl::tiltTask(void *arg) {
	while (1) {
		if (xQueueReceive(tiltCommandQueue, &tiltCurrentCommand, portMAX_DELAY)) {
			float pulse_frequency = (tiltCurrentCommand.rpm * 200) / 60.0;
			tiltCurrentCommand.complete = false;
			tiltPulseCount = 0;

			gpio_set_level((gpio_num_t) CONFIG_MOTR_T_PIN_DIR, tiltCurrentCommand.direction ? 1 : 0);
			mcpwm_set_frequency(STEPPER2_MCPWM_UNIT, STEPPER2_MCPWM_TIMER, (int)pulse_frequency);
			mcpwm_start(STEPPER2_MCPWM_UNIT, STEPPER2_MCPWM_TIMER);
			timer_start(TIMER_GROUP_0, TIMER_1);  // Start counting pulses
		}
	}
}

void setHoriCommand(int32_t steps, float rpm) {
	stepper_command_t command = {
		.steps = abs(steps),
		.rpm = rpm,
		.direction = steps > 0,
		.complete = false,
	};
	xQueueSend(horiCommandQueue, &command, portMAX_DELAY);
}

// Function to set a command for stepper 2
void setTiltCommand(int32_t steps, float rpm) {
	stepper_command_t command = {
		.steps = abs(steps),
		.rpm = rpm,
		.direction = steps > 0,
		.complete = false,
	};
	xQueueSend(tiltCommandQueue, &command, portMAX_DELAY);
}

MotorControl::MotorControl()
{
	pinMode(CONFIG_MOTR_H_PIN_DIR, OUTPUT);
	pinMode(CONFIG_MOTR_H_PIN_STEP, OUTPUT);

	pinMode(CONFIG_MOTR_T_PIN_DIR, OUTPUT);
	pinMode(CONFIG_MOTR_T_PIN_STEP, OUTPUT);

	if(CONFIG_MOTR_H_PIN_ENDSTOP >= 0){
		pinMode(CONFIG_MOTR_H_PIN_ENDSTOP, INPUT);
	}
	if(CONFIG_MOTR_T_PIN_ENDSTOP >= 0){ // needs external pullup
		pinMode(CONFIG_MOTR_T_PIN_ENDSTOP, INPUT);
	}
	stepperCompleteEventGroup = xEventGroupCreate();
	horiCommandQueue = xQueueCreate(10, sizeof(stepper_command_t));
	tiltCommandQueue = xQueueCreate(10, sizeof(stepper_command_t));

	mcpwmInit();
	timerInit();

	xTaskCreatePinnedToCore(horiTask, "horiTask", 2048, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(tiltTask, "tiltTask", 2048, NULL, 5, NULL, 0);


	setHoriCommand(40000, 120.0);  // Set stepper 1 to 120 RPM, 1000 steps, forward
	setTiltCommand(-4000, 90.0);  // Set stepper 2 to 90 RPM, 2000 steps, backward

	// Wait for both steppers to complete
	EventBits_t result = xEventGroupWaitBits(
			stepperCompleteEventGroup,
			STEPPER_COMPLETE_BIT_1 | STEPPER_COMPLETE_BIT_2,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY
			);

	if ((result & (STEPPER_COMPLETE_BIT_1 | STEPPER_COMPLETE_BIT_2)) == (STEPPER_COMPLETE_BIT_1 | STEPPER_COMPLETE_BIT_2)) {
		ESP_LOGI("Stepper", "Both steppers completed");
	}

	horiVariables.pause = RPM_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
	// tiltVariables.pause = RPM_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO);
	// xTaskCreatePinnedToCore(motorMoveTask, "motorMove", 2048, this,10, &motorMoveTaskHandle, 0);


}

uint8_t MotorControl::call(uint16_t id){
	switch(id){
		case TSID_MOTOR_HOME:
			homeRoutine(0);
			break;
	}

	return 0;


};

void MotorControl::motorMoveTask(void *arg){ // TODO pin to core 0
	static MotorControl* instance = (MotorControl*)arg;
	uint64_t time = 0;
	while(1){
		time=esp_timer_get_time();
		// if(time >= instance->horiVariables.nextStepTime){
		//	printf("time: %llu\n", time-instance->horiVariables.nextStepTime);
		//	instance->horiSingleStep(Direction::FORWARD);
		//	instance->horiVariables.nextStepTime = time + instance->horiVariables.pause;

		// programmed:
		//
		//		goto endTilt;
		//
		//
		// nonProgrammed:
		//		if (time - instance->horiVariables.lastStepTime > instance->horiVariables.pause){
		//			switch(instance->horiVariables.mode){
		//				case MotorMode::STOPPED:
		//					goto endHorz;
		//					break;
		//				case  MotorMode::HOMING:
		//					instance->horiSingleStep(Direction::FORWARD);
		//					break;
		//
		//				case MotorMode::STEPPER:
		//					if(instance->horiVariables.stepsToGo > 0){
		//						instance->horiSingleStep(Direction::FORWARD);
		//						instance->horiVariables.stepsToGo=instance->horiVariables.stepsToGo-1;
		//					}else if(instance->horiVariables.stepsToGo < 0){
		//						instance->horiSingleStep(Direction::BACKWARD);
		//						instance->horiVariables.stepsToGo=instance->horiVariables.stepsToGo+1;
		//					}
		//					break;
		//					break;
		//				case MotorMode::SPINDLE_CLOCKWISE:
		//					instance->horiSingleStep(Direction::FORWARD);
		//					break;
		//				case MotorMode::SPINDLE_COUNTERCLOCKWISE:
		//					instance->horiSingleStep(Direction::BACKWARD);
		//					break;
		//				default:
		//					break;
		//			}
		//			instance->horiVariables.lastStepTime = time;
		//		}
		// endHorz:
		// vTaskDelay(instance->horiVariables.pause < instance->horiVariables.pause ? instance->horiVariables.pause/port_TICK_PERIOD_US : instance->horiVariables.pause/port_TICK_PERIOD_US);
		vTaskDelay(100000);
	}
	vTaskDelete(NULL);

	}



	bool MotorControl::parseGcode(const char* gcode, uint16_t length)
	{
		float element = 0;


		// lambda that returns number following a given string
		// for example when gcode is "G0 X-1000 S2000" and we search for 'X' lambda returns -1000
		// if element is not found or data following it aren't just numbers NAN is returned
		auto getElement = [gcode, length](uint16_t index, const char* matchString, const uint16_t elementLength) -> float {
			bool negative = false;
			float toReturn = 0;
			uint8_t decimal = 0;
			for(uint16_t i = index; i < length; i++){
				if(gcode[i] == matchString[0]){
					if(strncmp(gcode+i, matchString, elementLength) == 0){
						i += elementLength;

						if(gcode[i] == '-'){
							negative = true;
							i++;
						}
						while(gcode[i] != ' ' && i < length){
							if(gcode[i] >= '0' && gcode[i] <= '9'){
								toReturn = toReturn * 10 + (gcode[i] - '0');
								if(decimal){
									decimal++;
								}
								i++;
								continue;
							}else if(gcode[i] == '.' && !decimal){
								decimal = 1;
								i++;
								continue;
							}
							return NAN;
						}
						toReturn /= pow(10, decimal-1);
#ifdef CONFIG_MOTR_DEBUG
						ESP_LOGI(TAG, "getElement | Found element %s: %f", matchString, negative ? -toReturn : toReturn);
#endif
						return negative ? -toReturn : toReturn;
					}
				}
			}
			return NAN;
		};



		if (strncmp(gcode, "M80", 3) == 0){ // power down high voltage supply
			horiStop();
			// tiltStop();
			// TODO
			return false;
		}else if (strncmp(gcode, "M81", 3) == 0){ // power up high voltage supply
																							// TODO
			return false;

		}else if (strncmp(gcode, "G20", 3) == 0){ // set unit to degrees
			unit=Unit::DEGREES;
			return true;
		}else if (strncmp(gcode, "G20", 3) == 0){ // set unit to steps
			unit=Unit::STEPS;
			return true;
		}else if (strncmp(gcode, "G90", 3) == 0){ // set the absolute positioning
			positioningMode = PositioningMode::ABSOLUTE;
			// tiltVariables.stepsToGo=0;
			horiVariables.stepsToGo=0;
			return true;
		}else if (strncmp(gcode, "G91", 3) == 0){ // set the relative positioning
			positioningMode = PositioningMode::RELATIVE;
			// tiltVariables.stepsToGo=0;
			horiVariables.stepsToGo=0;
			return true;
		}else if (strncmp(gcode, "G92", 3) == 0){ // set current position as home
			horiVariables.angle = 0;
			// tiltVariables.angle = 0;
			return true;
		}else if (strncmp(gcode, "G28", 3) == 0){ // home both driver

			TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_MOTOR_HOME, 1, 100, TaskPriority::TSK_PRIORITY_CRITICAL));
			return true;

		}else if (strncmp(gcode, "G0", 2) == 0){ // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
			element = getElement(3, "S", 1);
			if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
				// horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
				// tiltVariables.pause =  RPM_TO_PAUSE(element, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO);
#ifdef CONFIG_MOTR_DEBUG
				ESP_LOGI(TAG, "processRequestAT | General speed: %f", element);
#endif
			}

			// get the angles
			if(horiVariables.mode == MotorMode::STEPPER){

				element = getElement(3, "SH", 2);
				if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
					// horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
#ifdef CONFIG_MOTR_DEBUG
					ESP_LOGI(TAG, "processRequestAT | HorzMot speed: %f", element);
#endif
				}

				element = getElement(3, "H", 1);

				if(element != NAN){
					if(positioningMode == PositioningMode::ABSOLUTE){
						// -->
					}else{
						// horiVariables.stepsToGo = ANGLE_TO_STEP(element, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
					}
				}
			}
			//			if(tiltVariables.mode == MotorMode::STEPPER){
			//				element = getElement(3, "ST", 2);
			//				if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
			//					tiltVariables.pause =  RPM_TO_PAUSE(element, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO);
			// #ifdef CONFIG_MOTR_DEBUG
			//					ESP_LOGI(TAG, "processRequestAT | TiltMot speed: %f", element);
			// #endif
			//
			//				}
			//				element = getElement(3, "T", 1);
			//
			//				if(element != NAN){
			//					if(positioningMode == PositioningMode::ABSOLUTE){
			//						// -->
			//					}else{
			//						tiltVariables.stepsToGo = ANGLE_TO_STEP(element, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO);
			//					}
			//				}
			//			}

			return true;
		}else if (strncmp(gcode, "M03", 3) == 0){ // start spinning horzMot axis clockwise
			horiVariables.mode = MotorMode::STOPPED;
			horiVariables.stepsToGo = 0;
			element = getElement(3, "S", 1);
			// if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "processRequestAT | M03 speed is: %f", element);
#endif


			horiVariables.mode = MotorMode::SPINDLE_CLOCKWISE;
			return true;
		}else if (strncmp(gcode, "M04", 3) == 0){ // start spinning horzMot axis counterclockwise TODO: spindle mode should be supported on both motors
			horiVariables.mode = MotorMode::STOPPED;
			horiVariables.stepsToGo = 0;
			element = getElement(3, "S", 1);
			// if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "processRequestAT | M04 speed is: %f", element);
#endif
			horiVariables.mode = MotorMode::SPINDLE_CLOCKWISE;
			return true;
		}else if (strncmp(gcode, "M05", 3) == 0){ // stop spinning horzMot axis
			horiVariables.stepsToGo = 0;
			horiVariables.mode = MotorMode::STEPPER;
			return true;

		}else
			ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", gcode);
		return false;

	}

	// void MotorControl::tiltEndstopHandler(void *arg){
	//	MotorControl* instance = (MotorControl*)arg;
	//	if(instance->tiltVariables.mode == MotorMode::HOMING){
	//		instance->tiltVariables.mode = MotorMode::STOPPED;
	//	}
	//
	//	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horiVariables.mode == MotorMode::STOPPED && instance->tiltVariables.mode == MotorMode::STOPPED)
	//		instance->homeRoutine(1);
	//
	//	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horiVariables.mode == MotorMode::STOPPED && instance->tiltVariables.mode == MotorMode::STOPPED)
	//		instance->homeRoutine(2);
	// }

	void MotorControl::horizontalEndstopHandler(void *arg){
		MotorControl* instance = (MotorControl*)arg;
		if(instance->horiVariables.mode == MotorMode::HOMING){
			instance->horiVariables.mode = MotorMode::STOPPED;
		}

		if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horiVariables.mode == MotorMode::STOPPED && instance->tiltVariables.mode == MotorMode::STOPPED)
			instance->homeRoutine(1);

		if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horiVariables.mode == MotorMode::STOPPED && instance->tiltVariables.mode == MotorMode::STOPPED)
			instance->homeRoutine(2);
	}


	void MotorControl::homeRoutine(uint8_t part){}
	//		static PositioningMode preHomePosMode;
	//		static uint32_t preHomeHorzPause;
	//		static MotorMode preHomeHorzMode;
	//		static uint32_t preHomeTiltPause;
	//		static MotorMode preHomeTiltMode;
	//
	//		switch(part){
	//			case 0:
	// #ifdef CONFIG_MOTR_DEBUG
	//				ESP_LOGI(TAG, "homeRoutine | Starting homing routine");
	// #endif
	//				preHomeHorzPause = horiVariables.pause;
	//				// preHomeTiltPause = tiltVariables.pause;
	//				preHomePosMode = positioningMode;
	//				positioningMode = PositioningMode::HOMING_FAST;
	//				// TODO: register interrupts for both endstops
	//
	//				// tiltVariables.pause =RPM_TO_PAUSE(150, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO);
	//				horiVariables.pause =RPM_TO_PAUSE(150, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
	//				// if(CONFIG_MOTR_T_PIN_ENDSTOP >= 0){
	//				//	tiltVariables.mode = MotorMode::HOMING;
	//				//	attachInterruptArg(CONFIG_MOTR_T_PIN_ENDSTOP, tiltEndstopHandler, this, CHANGE);
	//				// }else
	//				//	tiltVariables.mode = MotorMode::STOPPED;
	//
	//
	//				if(CONFIG_MOTR_H_PIN_ENDSTOP >= 0){
	//					horiVariables.mode = MotorMode::HOMING;
	//					attachInterruptArg(CONFIG_MOTR_H_PIN_ENDSTOP, horizontalEndstopHandler, this, CHANGE);
	//				}else
	//					horiVariables.mode = MotorMode::STOPPED;
	//
	//				break; // NOTE: interrupt for both pin will check if both motors have stopped if so it will call homeRoutine(1)
	//			case 1: // both motors are stopped -> we move them bit
	//
	// #ifdef CONFIG_MOTR_DEBUG
	//				ESP_LOGI(TAG, "homeRoutine | Fast homing done, moving to slow homing");
	// #endif
	//				// if(CONFIG_MOTR_T_PIN_ENDSTOP >= 0){
	//				//	tiltVariables.pause = RPM_TO_PAUSE(10, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO);
	//				//	tiltStep(ANGLE_TO_STEP(5, CONFIG_MOTR_T_STEP_COUNT, CONFIG_MOTR_T_GEAR_RATIO));
	//				// }
	//
	//				if(CONFIG_MOTR_H_PIN_ENDSTOP >= 0){
	//					horiVariables.pause = RPM_TO_PAUSE(10, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO);
	//					horiStep(ANGLE_TO_STEP(5, CONFIG_MOTR_H_STEP_COUNT, CONFIG_MOTR_H_GEAR_RATIO));
	//				}
	//
	//				// activate motor mode again
	//				// tiltVariables.mode = CONFIG_MOTR_T_PIN_ENDSTOP >=0 ? MotorMode::HOMING : MotorMode::STOPPED;  // this will automatically change behavior in the motorMoveTask
	//				horiVariables.mode = CONFIG_MOTR_H_PIN_ENDSTOP >=0 ? MotorMode::HOMING : MotorMode::STOPPED;
	//
	//
	//				break;
	//
	//			case 2:
	// #ifdef CONFIG_MOTR_DEBUG
	//				ESP_LOGI(TAG, "homeRoutine | Motors homed");
	// #endif
	//				// tiltVariables.angle = 0;
	//				horiVariables.angle = 0;
	//
	//				horiVariables.pause = preHomeHorzPause;
	//				// tiltVariables.pause = preHomeTiltPause;
	//				positioningMode = preHomePosMode;
	//				// tiltVariables.pause = preHomeTiltPause;
	//				horiVariables.pause = preHomeHorzPause;
	//				// tiltVariables.mode = preHomeTiltMode;
	//				horiVariables.mode = preHomeHorzMode;
	//				// TODO: unregister interrupts for both endstops
	//				break;
	//		}
	//		// part one move fast to the endstop
	//	}
	//

void MotorControl::horiStop()
{
	gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN_DIR, LOW);
	gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN_STEP, LOW);
}

// void MotorControl::tiltStop()
// {
//	gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, LOW);
//	gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, LOW);
// }
//

