/*
 * stepper_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_control.h"

StepperControl stepperControl = StepperControl();


StepperControl::StepperControl(){}

void StepperControl::init()
{

	steppers.initMCPWN();
	steppers.initPCNT();
	StepperControl::homingEventGroup	 = xEventGroupCreate();


	if(CONFIG_STEPPER_H_PIN_ENDSTOP >= 0){
		pinMode(CONFIG_STEPPER_H_PIN_ENDSTOP, INPUT);
	}
	if(CONFIG_STEPPER_T_PIN_ENDSTOP >= 0){ // needs external pullup
		pinMode(CONFIG_STEPPER_T_PIN_ENDSTOP, INPUT);
	}

}




uint8_t StepperControl::call(uint16_t id){
	return 0;
};

void StepperControl::stepperMoveTask(void *arg){ // TODO pin to core 0
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

		// NOTE: all commands will be handled same we aren't in programm regime they will be added to to programm 0
		// all commands should be executed in motorTask -> added to a list and thats all

		if (strncmp(gcode, "M80", 3) == 0){ // power down high voltage supply
			// XXX this command will be executed immediately
			return false;
		}else if (strncmp(gcode, "M81", 3) == 0){ // power up high voltage supply
			// XXX this command will be executed immediately
			return false;

		}else if (strncmp(gcode, "G20", 3) == 0){ // set unit to degrees
			if(programmingMode == ProgrammingMode::NO_PROGRAMM){

			}else if (programmingMode == ProgrammingMode::PROGRAMMING){
				// TODO: stash command to programm queue
			}
			return true;
		}else if (strncmp(gcode, "G20", 3) == 0){ // set unit to steps
			if(programmingMode == ProgrammingMode::NO_PROGRAMM){
			}else if (programmingMode == ProgrammingMode::PROGRAMMING){
				// TODO: stash command to programm queue
			}
			return true;
		}else if (strncmp(gcode, "G90", 3) == 0){ // set the absolute positioning
			if(programmingMode == ProgrammingMode::NO_PROGRAMM){
			}else if (programmingMode == ProgrammingMode::PROGRAMMING){
				// TODO: stash command to programm queue
			}
			return true;
		}else if (strncmp(gcode, "G91", 3) == 0){ // set the relative positioning
			if(programmingMode == ProgrammingMode::NO_PROGRAMM){
			}else if (programmingMode == ProgrammingMode::PROGRAMMING){
				// TODO: stash command to programm queue
			}
			return true;
		}else if (strncmp(gcode, "G92", 3) == 0){ // set current position as home
			return true;
		}else if (strncmp(gcode, "G28", 3) == 0){ // home both drivers
			return true;

		}else if (strncmp(gcode, "G0", 2) == 0){ // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
			element = getElement(3, "S", 1);
			return true;
		}else if (strncmp(gcode, "M03", 3) == 0){ // start spinning horzMot axis clockwise
			return true;
		}else if (strncmp(gcode, "M04", 3) == 0){ // start spinning horzMot axis counterclockwise TODO: spindle mode should be supported on both steppers
			return true;
		}else if (strncmp(gcode, "M05", 3) == 0){ // stop spinning horzMot axis
			return true;

		}else
			return false;

	}

	void StepperControl::tiltEndstopHandler(void *arg){
		if(stepperControl.programmingMode == ProgrammingMode::HOMING){
			steppers.stopStepperH();
			xEventGroupSetBits(StepperControl::homingEventGroup, STEPPER_COMPLETE_BIT_H);
		}


	}

	void StepperControl::horizontalEndstopHandler(void *arg){
		if(stepperControl.programmingMode == ProgrammingMode::HOMING){
			steppers.stopStepperT();
			xEventGroupSetBits(StepperControl::homingEventGroup, STEPPER_COMPLETE_BIT_T);
		}
	}


	void StepperControl::home(){
		ESP_LOGI(TAG, "Home | Starting homing routine");
		// clear	the queues
		steppers.clearQueueH();
		steppers.clearQueueT();
		// stop the steppers
		steppers.stopStepperH();
		steppers.stopStepperT();
		// set the positioning mode to homing
		programmingMode = ProgrammingMode::HOMING;

		// attach interrupts
		attachInterruptArg(CONFIG_STEPPER_H_PIN_ENDSTOP, StepperControl::horizontalEndstopHandler, NULL, CHANGE);
		attachInterruptArg(CONFIG_STEPPER_T_PIN_ENDSTOP, StepperControl::tiltEndstopHandler, NULL, CHANGE);

		steppers.spindleStepperH(100, Direction::FORWARD);
		steppers.spindleStepperT(100, Direction::FORWARD);

		EventBits_t result = xEventGroupWaitBits(
				homingEventGroup,
				STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T,
				pdTRUE,
				pdTRUE,
				portMAX_DELAY
				);

		if(result & STEPPER_COMPLETE_BIT_H){
			ESP_LOGI(TAG, "Home | Horizontal stepper fast homed");
		}
		if(result & STEPPER_COMPLETE_BIT_T){
			ESP_LOGI(TAG, "Home | Tilt stepper fast homed");
		}


		steppers.stepStepperH(20, -10, true); // we will trigger stop commands on endstops, this shouldn't bother us it will just schedule stops to run
		steppers.stepStepperT(20, -10, true);

		steppers.spindleStepperH(1, Direction::FORWARD);
		steppers.spindleStepperT(1, Direction::FORWARD);

		result = xEventGroupWaitBits(
				homingEventGroup,
				STEPPER_COMPLETE_BIT_H | STEPPER_COMPLETE_BIT_T,
				pdTRUE,
				pdTRUE,
				portMAX_DELAY
				);

		if(result & STEPPER_COMPLETE_BIT_H){
			ESP_LOGI(TAG, "Home | Horizontal stepper slow homed");
		}
		if(result & STEPPER_COMPLETE_BIT_T){
			ESP_LOGI(TAG, "Home | Tilt stepper slow homed");
		}

		varsH.stepNumber.store(0);
		varsT.stepNumber.store(0);

		// cleanup
		xEventGroupClearBits(homingEventGroup, HOMING_DONE_BIT);
		detachInterrupt(CONFIG_STEPPER_H_PIN_ENDSTOP);
		detachInterrupt(CONFIG_STEPPER_T_PIN_ENDSTOP);

	}



