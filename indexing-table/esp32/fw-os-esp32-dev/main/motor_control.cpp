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
  xTaskCreate(task1, "motorMove", 2048, this,10, &myTask1Handle);
}

void motorMoveTask(void *arg){
	static PerStepperDriver* horizontal = (MotorControl*)arg->getHorizontal();
	static PerStepperDriver* tilt = (MotorControl*)arg->getTilt();
	static motorVariables* horizontalVar = &((MotorControl*)arg->horizontalMotorVariables);
	static motorVariables* tiltVar = &((MotorControl*)arg->tiltMotorVariables);
	uint64_t time = 0;

	while(1){
		time=esp_timer_get_time();

		if (horizontal!=nullptr && time - horizontalVar->lastStepTime > horizontalVar->pause){
			if(horizontal->getStepsToGo() > 0){ // we should check if mode is not spindle, however this will result at best in one
				horizontal->singleStep(Direction::FORWARD);
				horizontalVar->stepsToGo--;
			}else if(horizontal->getStepsToGo() < 0){
				horizontal->singleStep(Direction::BACKWARD);
				horizontalVar->stepsToGo++;
			}else if(horizontalVar->mode == MotorMode::SPINDLE_CLOCKWISE){
				horizontal->singleStep(Direction::FORWARD);
			}else if(horizontalVar->mode == MotorMode::SPINDLE_COUNTERCLOCKWISE){
				horizontal->singleStep(Direction::BACKWARD);
			}
			horizontalVar->lastStepTime = time;
		}

		if (tilt!=nullptr && tiltVar->stepsToGo != 0 && time - tiltVar->lastStepTime > tiltVar->pause){
			if(tilt->getStepsToGo() > 0){
				tilt->singleStep(Direction::FORWARD);
				tiltVar->stepsToGo--;
			}else{
				tilt->singleStep(Direction::BACKWARD);
				tiltVar->stepsToGo++;
			}
			tiltVar->lastStepTime = time;
		}

		vTaskDelay(horizontal->getStepPause() < tilt->getStepPause() ? horizontal->getStepPause()/port_TICK_PERIOD_US : tilt->getStepPause()/port_TICK_PERIOD_US);
	}

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
		horizontalStepsToGo=0;
	}else if (strncmp(request->command, "G91", 3) == 0){ // set the relative positioning
		tiltAngleRelativeStart = tiltAngle;
		horizontalAngleRelativeStart = horizontalAngle;
		positioningMode = PositioningMode::RELATIVE;
		tiltStepsToGo=0;
		horizontalStepsToGo=0;
	}else if (strncmp(request->command, "G92", 3) == 0){ // set current position as home
		horizontalVar->angle = 0;
		tiltVar->angle = 0;

	}else if (strncmp(request->command, "G28", 3) == 0){ // home both driver



	}else if (strncmp(request->command, "G0", 2) == 0){ // home to given position
	}else if (strncmp(request->command, "M03", 1) == 0){ // start spinning horizontal axis clockwise
		horizontalMotorVariables->stepsToGo = 0;
		horizontalMotorVariables->mode = MotorMode::SPINDLE_CLOCKWISE;
	}else if (strncmp(request->command, "M04", 1) == 0){ // start spinning horizontal axis counterclockwise
		horizontalMotorVariables->stepsToGo = 0;
		horizontalMotorVariables->mode = MotorMode::SPINDLE_CLOCKWISE;
	}else if (strncmp(request->command, "M05", 1) == 0){ // stop spinning horizontal axis
		horizontalMotorVariables->stepsToGo = 0;
		horizontalMotorVariables->mode = MotorMode::STEPPER;

	}else { // unknown command
		sendResponse(request, strERR, 5);
		ESP_LOGE(TAG, "processRequestAT | Unknown command: %s", request->command);
	}
	ESP_LOGE(TAG, "Gcode parsign not implemented");
}
