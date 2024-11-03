/*
 * stepper_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_control.h"

StepperControl* StepperControl::instance = nullptr;


StepperControl::StepperControl()
{

	steppers.initMCPWN();
	steppers.initPCNT();

	if(CONFIG_STEPPER_H_PIN_ENDSTOP >= 0){
		pinMode(CONFIG_STEPPER_H_PIN_ENDSTOP, INPUT);
	}
	if(CONFIG_STEPPER_T_PIN_ENDSTOP >= 0){ // needs external pullup
		pinMode(CONFIG_STEPPER_T_PIN_ENDSTOP, INPUT);
	}

	}
	// tiltVariables.pause = RPM_TO_PAUSE(CONFIG_STEPPER_DEFAULT_SPEED, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO);
	// xTaskCreatePinnedToCore(stepperMoveTask, "stepperMove", 2048, this,10, &stepperMoveTaskHandle, 0);




uint8_t StepperControl::call(uint16_t id){
	switch(id){
		case TSID_MOTOR_HOME:
			homeRoutine(0);
			break;
	}

	return 0;


};

void StepperControl::stepperMoveTask(void *arg){ // TODO pin to core 0
	static StepperControl* instance = (StepperControl*)arg;
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
		//				case StepperMode::STOPPED:
		//					goto endHorz;
		//					break;
		//				case  StepperMode::HOMING:
		//					instance->horiSingleStep(Direction::FORWARD);
		//					break;
		//
		//				case StepperMode::STEPPER:
		//					if(instance->horiVariables.stepsToGo > 0){
		//						instance->horiSingleStep(Direction::FORWARD);
		//						instance->horiVariables.stepsToGo=instance->horiVariables.stepsToGo-1;
		//					}else if(instance->horiVariables.stepsToGo < 0){
		//						instance->horiSingleStep(Direction::BACKWARD);
		//						instance->horiVariables.stepsToGo=instance->horiVariables.stepsToGo+1;
		//					}
		//					break;
		//					break;
		//				case StepperMode::SPINDLE_CLOCKWISE:
		//					instance->horiSingleStep(Direction::FORWARD);
		//					break;
		//				case StepperMode::SPINDLE_COUNTERCLOCKWISE:
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



	bool StepperControl::parseGcode(const char* gcode, uint16_t length)
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
#ifdef CONFIG_STEPPER_DEBUG
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
			// horiVariables.stepsToGo=0;
			return true;
		}else if (strncmp(gcode, "G91", 3) == 0){ // set the relative positioning
			positioningMode = PositioningMode::RELATIVE;
			// tiltVariables.stepsToGo=0;
			// horiVariables.stepsToGo=0;
			return true;
		}else if (strncmp(gcode, "G92", 3) == 0){ // set current position as home
			// horiVariables.angle = 0;
			// tiltVariables.angle = 0;
			return true;
		}else if (strncmp(gcode, "G28", 3) == 0){ // home both driver

			TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_MOTOR_HOME, 1, 100, TaskPriority::TSK_PRIORITY_CRITICAL));
			return true;

		}else if (strncmp(gcode, "G0", 2) == 0){ // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
			element = getElement(3, "S", 1);
			if(element != NAN && element > 0 && element <= CONFIG_STEPPER_MAX_SPEED){
				// horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
				// tiltVariables.pause =  RPM_TO_PAUSE(element, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO);
#ifdef CONFIG_STEPPER_DEBUG
				ESP_LOGI(TAG, "processRequestAT | General speed: %f", element);
#endif
			}

			// get the angles
			if(horiVariables.mode == StepperMode::STEPPER){

				element = getElement(3, "SH", 2);
				if(element != NAN && element > 0 && element <= CONFIG_STEPPER_MAX_SPEED){
					// horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
#ifdef CONFIG_STEPPER_DEBUG
					ESP_LOGI(TAG, "processRequestAT | HorzMot speed: %f", element);
#endif
				}

				element = getElement(3, "H", 1);

				if(element != NAN){
					if(positioningMode == PositioningMode::ABSOLUTE){
						// -->
					}else{
						// horiVariables.stepsToGo = ANGLE_TO_STEP(element, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
					}
				}
			}
			//			if(tiltVariables.mode == StepperMode::STEPPER){
			//				element = getElement(3, "ST", 2);
			//				if(element != NAN && element > 0 && element <= CONFIG_STEPPER_MAX_SPEED){
			//					tiltVariables.pause =  RPM_TO_PAUSE(element, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO);
			// #ifdef CONFIG_STEPPER_DEBUG
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
			//						tiltVariables.stepsToGo = ANGLE_TO_STEP(element, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO);
			//					}
			//				}
			//			}

			return true;
		}else if (strncmp(gcode, "M03", 3) == 0){ // start spinning horzMot axis clockwise
			horiVariables.mode = StepperMode::STOPPED;
			element = getElement(3, "S", 1);
			// if(element != NAN && element > 0 && element <= CONFIG_STEPPER_MAX_SPEED) horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
#ifdef CONFIG_STEPPER_DEBUG
			ESP_LOGI(TAG, "processRequestAT | M03 speed is: %f", element);
#endif


			horiVariables.mode = StepperMode::SPINDLE_CLOCKWISE;
			return true;
		}else if (strncmp(gcode, "M04", 3) == 0){ // start spinning horzMot axis counterclockwise TODO: spindle mode should be supported on both steppers
			horiVariables.mode = StepperMode::STOPPED;
			element = getElement(3, "S", 1);
			// if(element != NAN && element > 0 && element <= CONFIG_STEPPER_MAX_SPEED) horiVariables.pause =  RPM_TO_PAUSE(element, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
#ifdef CONFIG_STEPPER_DEBUG
			ESP_LOGI(TAG, "processRequestAT | M04 speed is: %f", element);
#endif
			horiVariables.mode = StepperMode::SPINDLE_CLOCKWISE;
			return true;
		}else if (strncmp(gcode, "M05", 3) == 0){ // stop spinning horzMot axis
			horiVariables.mode = StepperMode::STEPPER;
			return true;

		}else
			ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", gcode);
		return false;

	}

	// void StepperControl::tiltEndstopHandler(void *arg){
	//	StepperControl* instance = (StepperControl*)arg;
	//	if(instance->tiltVariables.mode == StepperMode::HOMING){
	//		instance->tiltVariables.mode = StepperMode::STOPPED;
	//	}
	//
	//	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horiVariables.mode == StepperMode::STOPPED && instance->tiltVariables.mode == StepperMode::STOPPED)
	//		instance->homeRoutine(1);
	//
	//	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horiVariables.mode == StepperMode::STOPPED && instance->tiltVariables.mode == StepperMode::STOPPED)
	//		instance->homeRoutine(2);
	// }

	void StepperControl::horizontalEndstopHandler(void *arg){
		StepperControl* instance = (StepperControl*)arg;
		if(instance->horiVariables.mode == StepperMode::HOMING){
			instance->horiVariables.mode = StepperMode::STOPPED;
		}

		if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horiVariables.mode == StepperMode::STOPPED && instance->tiltVariables.mode == StepperMode::STOPPED)
			instance->homeRoutine(1);

		if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horiVariables.mode == StepperMode::STOPPED && instance->tiltVariables.mode == StepperMode::STOPPED)
			instance->homeRoutine(2);
	}


	void StepperControl::homeRoutine(uint8_t part){}
	//		static PositioningMode preHomePosMode;
	//		static uint32_t preHomeHorzPause;
	//		static StepperMode preHomeHorzMode;
	//		static uint32_t preHomeTiltPause;
	//		static StepperMode preHomeTiltMode;
	//
	//		switch(part){
	//			case 0:
	// #ifdef CONFIG_STEPPER_DEBUG
	//				ESP_LOGI(TAG, "homeRoutine | Starting homing routine");
	// #endif
	//				preHomeHorzPause = horiVariables.pause;
	//				// preHomeTiltPause = tiltVariables.pause;
	//				preHomePosMode = positioningMode;
	//				positioningMode = PositioningMode::HOMING_FAST;
	//				// TODO: register interrupts for both endstops
	//
	//				// tiltVariables.pause =RPM_TO_PAUSE(150, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO);
	//				horiVariables.pause =RPM_TO_PAUSE(150, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
	//				// if(CONFIG_STEPPER_T_PIN_ENDSTOP >= 0){
	//				//	tiltVariables.mode = StepperMode::HOMING;
	//				//	attachInterruptArg(CONFIG_STEPPER_T_PIN_ENDSTOP, tiltEndstopHandler, this, CHANGE);
	//				// }else
	//				//	tiltVariables.mode = StepperMode::STOPPED;
	//
	//
	//				if(CONFIG_STEPPER_H_PIN_ENDSTOP >= 0){
	//					horiVariables.mode = StepperMode::HOMING;
	//					attachInterruptArg(CONFIG_STEPPER_H_PIN_ENDSTOP, horizontalEndstopHandler, this, CHANGE);
	//				}else
	//					horiVariables.mode = StepperMode::STOPPED;
	//
	//				break; // NOTE: interrupt for both pin will check if both steppers have stopped if so it will call homeRoutine(1)
	//			case 1: // both steppers are stopped -> we move them bit
	//
	// #ifdef CONFIG_STEPPER_DEBUG
	//				ESP_LOGI(TAG, "homeRoutine | Fast homing done, moving to slow homing");
	// #endif
	//				// if(CONFIG_STEPPER_T_PIN_ENDSTOP >= 0){
	//				//	tiltVariables.pause = RPM_TO_PAUSE(10, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO);
	//				//	tiltStep(ANGLE_TO_STEP(5, CONFIG_STEPPER_T_STEP_COUNT, CONFIG_STEPPER_T_GEAR_RATIO));
	//				// }
	//
	//				if(CONFIG_STEPPER_H_PIN_ENDSTOP >= 0){
	//					horiVariables.pause = RPM_TO_PAUSE(10, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO);
	//					horiStep(ANGLE_TO_STEP(5, CONFIG_STEPPER_H_STEP_COUNT, CONFIG_STEPPER_H_GEAR_RATIO));
	//				}
	//
	//				// activate stepper mode again
	//				// tiltVariables.mode = CONFIG_STEPPER_T_PIN_ENDSTOP >=0 ? StepperMode::HOMING : StepperMode::STOPPED;  // this will automatically change behavior in the stepperMoveTask
	//				horiVariables.mode = CONFIG_STEPPER_H_PIN_ENDSTOP >=0 ? StepperMode::HOMING : StepperMode::STOPPED;
	//
	//
	//				break;
	//
	//			case 2:
	// #ifdef CONFIG_STEPPER_DEBUG
	//				ESP_LOGI(TAG, "homeRoutine | Steppers homed");
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

void StepperControl::horiStop()
{
	gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_DIR, LOW);
	gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_STEP, LOW);
}

// void StepperControl::tiltStop()
// {
//	gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN1, LOW);
//	gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN2, LOW);
// }
//

