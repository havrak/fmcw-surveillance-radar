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

void MotorControl::setMotors(PerStepperDriver* horizontal, PerStepperDriver* tilt, uint8_t horizontalStepCount = 200, uint8_t tiltStepCount = 200, uint8_t horizontalGearRatio =1 , uint8_t tiltGearRatio = 1){
	this->horizontal = horizontal;
	this->tilt = tilt;
	this->horzVar.stepCount = horizontalStepCount;
	this->tiltVar.stepCount = tiltStepCount;
	this->horzVar.gearRatio = horizontalGearRatio;
	this->tiltVar.gearRatio = tiltGearRatio;
	this->horzVar.endstop = horizontalEndstop;
	this->tiltVar.endstop = tiltEndstop;
	this->horzVar.angleMax = horizontalAngleMax;
	this->tiltVar.angleMax = tiltAngleMax;
	this->horzVar.angleMin = horizontalAngleMin;
	this->tiltVar.angleMin = tiltAngleMin;
	if(horizontal != nullptr && tilt != nullptr){
		xTaskCreate(motorMoveTask, "motorMove", 2048, this,10, &motorMoveTaskHandle);
	}
	this->horzVar.pause = RMP_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, horizontalStepCount, horizontalGearRatio);
	this->tiltVar.pause = RMP_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, tiltStepCount, tiltGearRatio);
}

void motorMoveTask(void *arg){
	static PerStepperDriver* horzMot = (MotorControl*)arg->getHorizontal();
	static PerStepperDriver* tilt = (MotorControl*)arg->getTilt();
	static motorVariables* horzVar = &((MotorControl*)arg->horzVar);
	static motorVariables* tiltVar = &((MotorControl*)arg->tiltVar);
	uint64_t time = 0;

	if(horzMot == nullptr || tilt == nullptr){
		ESP_LOGE(TAG, "motorMoveTask | Both motors are not set");
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
						horzVar->stepsToGo--;
					}else if(horzVar->stepsToGo < 0){
						horzMot->singleStep(Direction::BACKWARD);
						horzVar->stepsToGo++;
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
						tiltVar->stepsToGo--;
					}else if(tiltVar->stepsToGo < 0){
						horzMot->singleStep(Direction::BACKWARD);
						tiltVar->stepsToGo++;
					}
					break;
				default:
					break;
			}

			tiltVar->lastStepTime = time;
		}

endTilt:
		vTaskDelay(horzMot->pause < tilt->pause ? horzMot->pause/port_TICK_PERIOD_US : tilt->pause/port_TICK_PERIOD_US);
	}
endTask:
	vTaskDelete(NULL);

}



bool MotorControl::parseGcode(const char* gcode, uint16_t length)
{
	static int32_t element = 0;


	// lambda that returns number following a given string
	// for example when gcode is "G0 X-1000 S2000" and we search for 'X' labda returns -1000
	// if element is not found or data following it aren't just numbers number 0xFF'FF'FF'FF is returned
	auto getElement = [gcode, length](uint16_t index, const char* matchString, const uint16_t elementLength) -> float {
		int32_t toReturn = 0;
		bool negative = false;
		bool decimal = false;

		for(uint16_t = index; i < length; i++){
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
							i++;
						}else if(gcode[i] == '.' && !decimal){
							decimal = true;
							// each subsequent digit is past decimal point
							i++;
						}
							return 0xFFFFFFFF;
					}
					return negative ? -toReturn : toReturn;
				}
			}

		}
		return 0xFFFFFFFF;
	};



	if (strncmp(request->command, "M80", 3) == 0){ // power down high voltage supply
		horzMot->stop();
		tiltMot->stop();
		// TODO
	}else if (strncmp(request->command, "M81", 3) == 0){ // power up high voltage supply
		// TODO
	}else if (strncmp(request->command, "G90", 3) == 0){ // set the absolute positioning
		positioningMode = PositioningMode::ABSOLUTE;
		tiltStepsToGo=0;
		horzMotStepsToGo=0;
		return true;
	}else if (strncmp(request->command, "G91", 3) == 0){ // set the relative positioning
		tiltAngleRelativeStart = tiltAngle;
		horzMotAngleRelativeStart = horzMotAngle;
		positioningMode = PositioningMode::RELATIVE;
		tiltStepsToGo=0;
		horzMotStepsToGo=0;
		return true;
	}else if (strncmp(request->command, "G92", 3) == 0){ // set current position as home
		horzVar->angle = 0;
		tiltVar->angle = 0;
		return true;

	}else if (strncmp(request->command, "G28", 3) == 0){ // home both driver

		TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_MOTOR_HOME, 1, 100, 10));
		return true;

	}else if (strncmp(request->command, "G0", 2) == 0){ // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
		element = getElement(3, "S", 1);
		if(element != 0xFFFFFFFF && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
			horzVar->pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio)
			tiltVar->pause =  RPM_TO_PAUSE(element, tiltVar.stepCount, tiltVar.gearRatio);
		}
		element = getElement(3, "SH", 2);
		if(element != 0xFFFFFFFF && element > 0 && element <= CONFIG_MOTR_MAX_SPEED)
			horzVar->pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio)

		element = getElement(3, "ST", 2);
		if(element != 0xFFFFFFFF && element > 0 && element <= CONFIG_MOTR_MAX_SPEED)
			tiltVar->pause =  RPM_TO_PAUSE(element, tiltVar.stepCount, tiltVar.gearRatio);

		// get the angles
		element = getElement(3, "H", 1);
		if(element != 0xFFFFFFFF){
			if(positioningMode == PositioningMode::ABSOLUTE){
				horzVar->stepsToGo = ANGLE_TO_STEP(element, horzVar.stepCount, horzVar.gearRatio) - horzVar->angle;
			}else{
				horzVar->stepsToGo = ANGLE_TO_STEP(element, horzVar.stepCount, horzVar.gearRatio);
			}
		}


	}else if (strncmp(request->command, "M03", 3) == 0){ // start spinning horzMot axis clockwise
		horzVar->mode = MotorMode::STOPPED;
		horzVar->stepsToGo = 0;
		element = getElement(3, "S", 1);
		if(element != 0xFFFFFFFF && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horzVar->pause =  RPM_TO_PAUSE(element, horzVar.stepCount, horzVar.gearRatio);


		horzVar->mode = MotorMode::SPINDLE_CLOCKWISE;
	}else if (strncmp(request->command, "M04", 3) == 0){ // start spinning horzMot axis counterclockwise
		horzVar->mode = MotorMode::STOPPED;
		horzVar->stepsToGo = 0;
		elemenet = getElement(3, "S", 1);
		if(elemenet != 0xFFFFFFFF && elemenet > 0 && elemenet <= CONFIG_MOTR_MAX_SPEED) horzVar->pause =  RPM_TO_PAUSE(elemenet, horzVar.stepCount, horzVar.gearRatio);
		horzVar->mode = MotorMode::SPINDLE_CLOCKWISE;
	}else if (strncmp(request->command, "M05", 3) == 0){ // stop spinning horzMot axis
		horzVar->stepsToGo = 0;
		horzVar->mode = MotorMode::STEPPER;

	}else { // unknown command
		sendResponse(request, strERR, 5);
		ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", request->command);
	}
	ESP_LOGE(TAG, "Gcode parsign not implemented");
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
	if(instance->tiltVar->mode == MotorMode::HOMING){
		instance->tiltVar->mode = MotorMode::STOPPED;
	}

	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horzVar->mode == MotorMode::STOPPED && instance->tiltVar->mode == MotorMode::STOPPED)
		instance->homeRoutine(1);

	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horzVar->mode == MotorMode::STOPPED && instance->tiltVar->mode == MotorMode::STOPPED)
		instance->homeRoutine(2);
}

void MotorControl::horizontalEndstopHandler(void *arg){
	MotorControl* instance = (MotorControl*)arg;
	if(instance->horzVar->mode == MotorMode::HOMING){
		instance->horzVar->mode = MotorMode::STOPPED;
	}

	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horzVar->mode == MotorMode::STOPPED && instance->tiltVar->mode == MotorMode::STOPPED)
		instance->homeRoutine(1);

	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horzVar->mode == MotorMode::STOPPED && instance->tiltVar->mode == MotorMode::STOPPED)
		instance->homeRoutine(2);
}


void MotorControl::homeRoutine(uint8_t part){
	static PositioningMode preHomePosMode = PositioningMode::ABSOLUTE;
	static uint32_t preHomeHorzPause = 0;
	static MotorMode preHomeHorzMode = 0;
	static uint32_t preHomeTiltPause = 0;
	static MotorMode preHomeTiltMode = 0;

	switch(part){
		case 0:
			preHomeHorzPause = horzVar->pause;
			preHomeTiltPause = tiltVar->pause;
			preHomePosMode = positioningMode;
			positioningMode = PositioningMode::HOMING_FAST;
			// TODO: register interrupts for both endstops

			tiltVar->pause =RMP_TO_PAUSE(150, tiltVar->stepCount, tiltVar.gearRatio);
			horzVar->pause =RMP_TO_PAUSE(150, horzVar->stepCount, horzVar.gearRatio);
			if(tiltVar->endstop >= 0){
				tiltVar->mode = MotorMode::HOMING;
				attachInterruptArg(tiltVar->endstop, tiltEndstopHandler, this, CHANGE);
			}else
				tiltVar->mode = MotorMode::STOPPED;


			if(horzVar->endstop >= 0){
				horzVar->mode = MotorMode::HOMING;
				attachInterruptArg(horzVar->endstop, horizontalEndstopHandler, this, CHANGE);
			}else
				horzVar->mode = MotorMode::STOPPED;

			break; // NOTE: interrupt for both pin will check if both motors have stopped if so it will call homeRoutine(1)
		case 1: // both motors are stopped -> we move them bit

			if(tiltVar->endstop >= 0){
				tiltVar->pause = RMP_TO_PAUSE(10, tiltVar->stepCount, tiltVar.gearRatio);
				tiltMot->setStepPause(tiltVar->pause);
				tiltMot->step(ANGLE_TO_STEP(5, tiltVar.stepCount, tiltVar.gearRatio));
			}

			if(horzVar->endstop >= 0){
				horzVar->pause = RMP_TO_PAUSE(10, horzVar->stepCount, horzVar.gearRatio);
				horzMot->setStepPause(horzVar->pause);
				horzMot->step(ANGLE_TO_STEP(5, horzVar.stepCount, horzVar.gearRatio));
			}

			// activate motor mode again
			tiltVar->mode = tiltVar.endstop >=0 ? MotorMode::HOMING : MotorMode::STOPPED;  // this will automatically change behavior in the motorMoveTask
			horzVar->mode = horzVar.endstop >=0 ? MotorMode::HOMING : MotorMode::STOPPED;


			break;

		case 2:
			tiltVar->angle = 0;
			horzVar->angle = 0;

			horzVar->pause = preHomeHorzPause;
			tiltVar->pause = preHomeTiltPause;
			positioningMode = preHomePosMode;
			tiltVar->pause = preHomeTiltPause;
			horzVar->pause = preHomeHorzPause;
			tiltVar->mode = preHomeTiltMode;
			horzVar->mode = preHomeHorzMode;

			// TODO: unregister interrupts for both endstops

			break;
	}
	// part one move fast to the endstop

}
