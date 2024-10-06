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

void MotorControl::startMotorTask(){
	xTaskCreate(motorMoveTask, "motorMove", 2048, this,10, &motorMoveTaskHandle);
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
				case  MotorMode::HOME:
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
				case  MotorMode::HOME:
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



void MotorControl::parseGcode(const char* gcode, uint16_t length)
{

	auto routineGetInfo = [request, this]() { // return device info
																						// -> call to os
	};
	if (strncmp(request->command, "M80", 3) == 0){ // power down high voltage supply
	}else if (strncmp(request->command, "M81", 3) == 0){ // power up high voltage supply
	}else if (strncmp(request->command, "G90", 3) == 0){ // set the absolute positioning
		positioningMode = PositioningMode::ABSOLUTE;
		tiltStepsToGo=0;
		horzMotStepsToGo=0;
	}else if (strncmp(request->command, "G91", 3) == 0){ // set the relative positioning
		tiltAngleRelativeStart = tiltAngle;
		horzMotAngleRelativeStart = horzMotAngle;
		positioningMode = PositioningMode::RELATIVE;
		tiltStepsToGo=0;
		horzMotStepsToGo=0;
	}else if (strncmp(request->command, "G92", 3) == 0){ // set current position as home
		horzVar->angle = 0;
		tiltVar->angle = 0;

	}else if (strncmp(request->command, "G28", 3) == 0){ // home both driver



	}else if (strncmp(request->command, "G0", 2) == 0){ // home to given position
	}else if (strncmp(request->command, "M03", 1) == 0){ // start spinning horzMot axis clockwise
		horzVar->stepsToGo = 0;
		horzVar->mode = MotorMode::SPINDLE_CLOCKWISE;
	}else if (strncmp(request->command, "M04", 1) == 0){ // start spinning horzMot axis counterclockwise
		horzVar->stepsToGo = 0;
		horzVar->mode = MotorMode::SPINDLE_CLOCKWISE;
	}else if (strncmp(request->command, "M05", 1) == 0){ // stop spinning horzMot axis
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
		// setup interrupt

	}
	if(tiltEndstop >= 0){
		this->tiltVar.endstop = tiltEndstop;
		// setup interrupt

	}
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
			tiltVar->mode = MotorMode::HOME;  // this will automatically change behavior in the motorMoveTask
			horzVar->mode = MotorMode::HOME;
			break; // NOTE: interrupt for both pin will check if both motors have stopped if so it will call homeRoutine(1)
		case 1: // both motors are stopped -> we move them bit

			tiltVar->pause = RMP_TO_PAUSE(10, tiltVar->stepCount, tiltVar.gearRatio);
			horzVarVar->pause = -RMP_TO_PAUSE(10, tiltVar->stepCount, tiltVar.gearRatio);

			tiltMot->setStepPause(tiltVar->pause);
			horzMot->setStepPause(horzVar->pause);
			tiltMot->step(ANGLE_TO_STEP(5, tiltVar.stepCount, tiltVar.gearRatio));
			horzMot->step(ANGLE_TO_STEP(5, horzVar.stepCount, horzVar.gearRatio));

			// activate motor mode again
			tiltVar->mode = MotorMode::HOME;  // this will automatically change behavior in the motorMoveTask
			horzVar->mode = MotorMode::HOME;


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
