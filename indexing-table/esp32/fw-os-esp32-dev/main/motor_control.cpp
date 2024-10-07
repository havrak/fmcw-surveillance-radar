/*
 * motor_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "motor_control.h"

MotorControl* MotorControl::instance = nullptr;

MotorControl::MotorControl()
{
}

uint8_t MotorControl::call(uint16_t id){
	switch(id){
		case TSID_MOTOR_HOME:
			homeRoutine(0);
			break;
	}

	return 0;


};

void MotorControl::setMotors(PerStepperDriver* horizontal, PerStepperDriver* tilt, uint8_t horizontalStepCount, uint8_t tiltStepCount, uint8_t horizontalGearRatio, uint8_t tiltGearRatio){
	this->horzMot = horizontal;
	this->tiltMot = tilt;
	this->horzVar.stepCount = horizontalStepCount;
	this->tiltVar.stepCount = tiltStepCount;
	this->horzVar.gearRatio = horizontalGearRatio;
	this->tiltVar.gearRatio = tiltGearRatio;
	if(horizontal != nullptr && tilt != nullptr){
		xTaskCreate(motorMoveTask, "motorMove", 2048, this,10, &motorMoveTaskHandle);
	}
	this->horzVar.pause = RPM_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, horizontalStepCount, horizontalGearRatio);
	this->tiltVar.pause = RPM_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, tiltStepCount, tiltGearRatio);
}

void MotorControl::motorMoveTask(void *arg){
	static MotorControl* instance = (MotorControl*)arg;
	static PerStepperDriver* horzMot = ((MotorControl*)arg)->horzMot;
	static PerStepperDriver* tilt = ((MotorControl*)arg)->tiltMot;
	static volatile motorVariables* horzVar = &(((MotorControl*)arg)->horzVar);
	static volatile motorVariables* tiltVar = &(((MotorControl*)arg)->tiltVar);
	uint64_t time = 0;

	if(horzMot == nullptr || tilt == nullptr){
		ESP_LOGE(instance->TAG, "motorMoveTask | Both motors are not set");
		goto endTask;
	}

	while(1){
		time=esp_timer_get_time();

		if (time - horzVar->lastStepTime > horzVar->pause){
			switch(horzVar->mode){
				case MotorMode::STOPPED:
					goto endHorz;
					break;
				case  MotorMode::HOMING:
					horzMot->singleStep(Direction::FORWARD);
					break;

				case MotorMode::STEPPER:
					if(horzVar->stepsToGo > 0){
						horzMot->singleStep(Direction::FORWARD);
						horzVar->stepsToGo=horzVar->stepsToGo-1; // NOTE -- expression isn't atomic
					}else if(horzVar->stepsToGo < 0){
						horzMot->singleStep(Direction::BACKWARD);
						horzVar->stepsToGo=horzVar->stepsToGo+1;
					}
					break;
					break;
				case MotorMode::SPINDLE_CLOCKWISE:
					horzMot->singleStep(Direction::FORWARD);
					break;
				case MotorMode::SPINDLE_COUNTERCLOCKWISE:
					horzMot->singleStep(Direction::BACKWARD);
					break;
				default:
					break;
			}
			horzVar->lastStepTime = time;
		}
endHorz:
		if (time - tiltVar->lastStepTime > tiltVar->pause){
			switch(horzVar->mode){
				case MotorMode::STOPPED:
					goto endTilt;
					break;
				case  MotorMode::HOMING:
					horzMot->singleStep(Direction::BACKWARD);
					break;
				case MotorMode::STEPPER:
					if(tiltVar->stepsToGo > 0){
						horzMot->singleStep(Direction::FORWARD);
						tiltVar->stepsToGo=tiltVar->stepsToGo-1;
					}else if(tiltVar->stepsToGo < 0){
						horzMot->singleStep(Direction::BACKWARD);
						tiltVar->stepsToGo=tiltVar->stepsToGo+1;
					}
					break;
				default:
					break;
			}

			tiltVar->lastStepTime = time;
		}

endTilt:
		vTaskDelay(horzVar->pause < tiltVar->pause ? horzVar->pause/port_TICK_PERIOD_US : tiltVar->pause/port_TICK_PERIOD_US);
	}
endTask:
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
		horzMot->stop();
		tiltMot->stop();
		// TODO
		return false;
	}else if (strncmp(gcode, "M81", 3) == 0){ // power up high voltage supply
																											 // TODO
		return false;

	}else if (strncmp(gcode, "G90", 3) == 0){ // set the absolute positioning
		positioningMode = PositioningMode::ABSOLUTE;
		tiltVar.stepsToGo=0;
		horzVar.stepsToGo=0;
		return true;
	}else if (strncmp(gcode, "G91", 3) == 0){ // set the relative positioning
		positioningMode = PositioningMode::RELATIVE;
		tiltVar.stepsToGo=0;
		horzVar.stepsToGo=0;
		return true;
	}else if (strncmp(gcode, "G92", 3) == 0){ // set current position as home
		horzVar.angle = 0;
		tiltVar.angle = 0;
		return true;
	}else if (strncmp(gcode, "G28", 3) == 0){ // home both driver

		TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_MOTOR_HOME, 1, 100, TaskPriority::TSK_PRIORITY_CRITICAL));
		return true;

	}else if (strncmp(gcode, "G0", 2) == 0){ // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
		element = getElement(3, "S", 1);
		if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
			horzVar.pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio);
			tiltVar.pause =  RPM_TO_PAUSE(element, tiltVar.stepCount, tiltVar.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "processRequestAT | General speed: %f", element);
#endif
		}

		// get the angles
		if(horzVar.mode == MotorMode::STEPPER){

			element = getElement(3, "SH", 2);
			if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
				horzVar.pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
				ESP_LOGI(TAG, "processRequestAT | HorzMot speed: %f", element);
#endif
			}

			element = getElement(3, "H", 1);

			if(element != NAN){
				if(positioningMode == PositioningMode::ABSOLUTE){
					// -->
				}else{
					horzVar.stepsToGo = ANGLE_TO_STEP(element, horzVar.stepCount, horzVar.gearRatio);
				}
			}
		}
		if(tiltVar.mode == MotorMode::STEPPER){
			element = getElement(3, "ST", 2);
			if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
				tiltVar.pause =  RPM_TO_PAUSE(element, tiltVar.stepCount, tiltVar.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
				ESP_LOGI(TAG, "processRequestAT | TiltMot speed: %f", element);
#endif

			}
			element = getElement(3, "T", 1);

			if(element != NAN){
				if(positioningMode == PositioningMode::ABSOLUTE){
					// -->
				}else{
					tiltVar.stepsToGo = ANGLE_TO_STEP(element, tiltVar.stepCount, tiltVar.gearRatio);
				}
			}
		}

		return true;
	}else if (strncmp(gcode, "M03", 3) == 0){ // start spinning horzMot axis clockwise
		horzVar.mode = MotorMode::STOPPED;
		horzVar.stepsToGo = 0;
		element = getElement(3, "S", 1);
		if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horzVar.pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
		ESP_LOGI(TAG, "processRequestAT | M03 speed is: %f", element);
#endif


		horzVar.mode = MotorMode::SPINDLE_CLOCKWISE;
		return true;
	}else if (strncmp(gcode, "M04", 3) == 0){ // start spinning horzMot axis counterclockwise
		horzVar.mode = MotorMode::STOPPED;
		horzVar.stepsToGo = 0;
		element = getElement(3, "S", 1);
		if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horzVar.pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
		ESP_LOGI(TAG, "processRequestAT | M04 speed is: %f", element);
#endif
		horzVar.mode = MotorMode::SPINDLE_CLOCKWISE;
		return true;
	}else if (strncmp(gcode, "M05", 3) == 0){ // stop spinning horzMot axis
		horzVar.stepsToGo = 0;
		horzVar.mode = MotorMode::STEPPER;
		return true;

	}else
	ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", gcode);
	return false;

}

void MotorControl::setEndstops(int8_t horzMotEndstop, int8_t tiltEndstop){
	if(horzMotEndstop >= 0){
		this->horzVar.endstop = horzMotEndstop;
	}
	if(tiltEndstop >= 0){
		this->tiltVar.endstop = tiltEndstop;
	}
}

void MotorControl::tiltEndstopHandler(void *arg){
	MotorControl* instance = (MotorControl*)arg;
	if(instance->tiltVar.mode == MotorMode::HOMING){
		instance->tiltVar.mode = MotorMode::STOPPED;
	}

	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horzVar.mode == MotorMode::STOPPED && instance->tiltVar.mode == MotorMode::STOPPED)
		instance->homeRoutine(1);

	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horzVar.mode == MotorMode::STOPPED && instance->tiltVar.mode == MotorMode::STOPPED)
		instance->homeRoutine(2);
}

void MotorControl::horizontalEndstopHandler(void *arg){
	MotorControl* instance = (MotorControl*)arg;
	if(instance->horzVar.mode == MotorMode::HOMING){
		instance->horzVar.mode = MotorMode::STOPPED;
	}

	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horzVar.mode == MotorMode::STOPPED && instance->tiltVar.mode == MotorMode::STOPPED)
		instance->homeRoutine(1);

	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horzVar.mode == MotorMode::STOPPED && instance->tiltVar.mode == MotorMode::STOPPED)
		instance->homeRoutine(2);
}


void MotorControl::homeRoutine(uint8_t part){
	static PositioningMode preHomePosMode;
	static uint32_t preHomeHorzPause;
	static MotorMode preHomeHorzMode;
	static uint32_t preHomeTiltPause;
	static MotorMode preHomeTiltMode;

	switch(part){
		case 0:
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "homeRoutine | Starting homing routine");
#endif
			preHomeHorzPause = horzVar.pause;
			preHomeTiltPause = tiltVar.pause;
			preHomePosMode = positioningMode;
			positioningMode = PositioningMode::HOMING_FAST;
			// TODO: register interrupts for both endstops

			tiltVar.pause =RPM_TO_PAUSE(150, tiltVar.stepCount, tiltVar.gearRatio);
			horzVar.pause =RPM_TO_PAUSE(150, horzVar.stepCount, horzVar.gearRatio);
			if(tiltVar.endstop >= 0){
				tiltVar.mode = MotorMode::HOMING;
				attachInterruptArg(tiltVar.endstop, tiltEndstopHandler, this, CHANGE);
			}else
				tiltVar.mode = MotorMode::STOPPED;


			if(horzVar.endstop >= 0){
				horzVar.mode = MotorMode::HOMING;
				attachInterruptArg(horzVar.endstop, horizontalEndstopHandler, this, CHANGE);
			}else
				horzVar.mode = MotorMode::STOPPED;

			break; // NOTE: interrupt for both pin will check if both motors have stopped if so it will call homeRoutine(1)
		case 1: // both motors are stopped -> we move them bit

#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "homeRoutine | Fast homing done, moving to slow homing");
#endif
			if(tiltVar.endstop >= 0){
				tiltVar.pause = RPM_TO_PAUSE(10, tiltVar.stepCount, tiltVar.gearRatio);
				tiltMot->setStepPause(tiltVar.pause);
				tiltMot->step(ANGLE_TO_STEP(5, tiltVar.stepCount, tiltVar.gearRatio));
			}

			if(horzVar.endstop >= 0){
				horzVar.pause = RPM_TO_PAUSE(10, horzVar.stepCount, horzVar.gearRatio);
				horzMot->setStepPause(horzVar.pause);
				horzMot->step(ANGLE_TO_STEP(5, horzVar.stepCount, horzVar.gearRatio));
			}

			// activate motor mode again
			tiltVar.mode = tiltVar.endstop >=0 ? MotorMode::HOMING : MotorMode::STOPPED;  // this will automatically change behavior in the motorMoveTask
			horzVar.mode = horzVar.endstop >=0 ? MotorMode::HOMING : MotorMode::STOPPED;


			break;

		case 2:
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "homeRoutine | Motors homed");
#endif
			tiltVar.angle = 0;
			horzVar.angle = 0;

			horzVar.pause = preHomeHorzPause;
			tiltVar.pause = preHomeTiltPause;
			positioningMode = preHomePosMode;
			tiltVar.pause = preHomeTiltPause;
			horzVar.pause = preHomeHorzPause;
			tiltVar.mode = preHomeTiltMode;
			horzVar.mode = preHomeHorzMode;

			// TODO: unregister interrupts for both endstops

			break;
	}
	// part one move fast to the endstop

}
