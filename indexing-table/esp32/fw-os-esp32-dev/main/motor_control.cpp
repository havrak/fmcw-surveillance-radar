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
	this->horiVariables.stepCount = horizontalStepCount;
	this->tiltVariables.stepCount = tiltStepCount;
	this->horiVariables.gearRatio = horizontalGearRatio;
	this->tiltVariables.gearRatio = tiltGearRatio;
	if(horizontal != nullptr && tilt != nullptr){
		xTaskCreate(motorMoveTask, "motorMove", 2048, this,10, &motorMoveTaskHandle);
	}
	this->horiVariables.pause = RPM_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, horizontalStepCount, horizontalGearRatio);
	this->tiltVariables.pause = RPM_TO_PAUSE(CONFIG_MOTR_DEFAULT_SPEED, tiltStepCount, tiltGearRatio);
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

	}else if (strncmp(gcode, "G20", 3) == 0){ // set unit to degrees
		unit=Unit::DEGREES;
		return true;
	}else if (strncmp(gcode, "G20", 3) == 0){ // set unit to steps
		unit=Unit::STEPS;
		return true;
	}else if (strncmp(gcode, "G90", 3) == 0){ // set the absolute positioning
		positioningMode = PositioningMode::ABSOLUTE;
		tiltVariables.stepsToGo=0;
		horiVariables.stepsToGo=0;
		return true;
	}else if (strncmp(gcode, "G91", 3) == 0){ // set the relative positioning
		positioningMode = PositioningMode::RELATIVE;
		tiltVariables.stepsToGo=0;
		horiVariables.stepsToGo=0;
		return true;
	}else if (strncmp(gcode, "G92", 3) == 0){ // set current position as home
		horiVariables.angle = 0;
		tiltVariables.angle = 0;
		return true;
	}else if (strncmp(gcode, "G28", 3) == 0){ // home both driver

		TaskerSingletonWrapper::getInstance()->addTask(new Task(this, TSID_MOTOR_HOME, 1, 100, TaskPriority::TSK_PRIORITY_CRITICAL));
		return true;

	}else if (strncmp(gcode, "G0", 2) == 0){ // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
		element = getElement(3, "S", 1);
		if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
			horiVariables.pause =  RPM_TO_PAUSE(element, horiVariables.stepCount, horiVariables.gearRatio);
			tiltVariables.pause =  RPM_TO_PAUSE(element, tiltVariables.stepCount, tiltVariables.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "processRequestAT | General speed: %f", element);
#endif
		}

		// get the angles
		if(horiVariables.mode == MotorMode::STEPPER){

			element = getElement(3, "SH", 2);
			if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
				horiVariables.pause =  RPM_TO_PAUSE(element, horiVariables.stepCount, horiVariables.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
				ESP_LOGI(TAG, "processRequestAT | HorzMot speed: %f", element);
#endif
			}

			element = getElement(3, "H", 1);

			if(element != NAN){
				if(positioningMode == PositioningMode::ABSOLUTE){
					// -->
				}else{
					horiVariables.stepsToGo = ANGLE_TO_STEP(element, horiVariables.stepCount, horiVariables.gearRatio);
				}
			}
		}
		if(tiltVariables.mode == MotorMode::STEPPER){
			element = getElement(3, "ST", 2);
			if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED){
				tiltVariables.pause =  RPM_TO_PAUSE(element, tiltVariables.stepCount, tiltVariables.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
				ESP_LOGI(TAG, "processRequestAT | TiltMot speed: %f", element);
#endif

			}
			element = getElement(3, "T", 1);

			if(element != NAN){
				if(positioningMode == PositioningMode::ABSOLUTE){
					// -->
				}else{
					tiltVariables.stepsToGo = ANGLE_TO_STEP(element, tiltVariables.stepCount, tiltVariables.gearRatio);
				}
			}
		}

		return true;
	}else if (strncmp(gcode, "M03", 3) == 0){ // start spinning horzMot axis clockwise
		horiVariables.mode = MotorMode::STOPPED;
		horiVariables.stepsToGo = 0;
		element = getElement(3, "S", 1);
		if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horiVariables.pause =  RPM_TO_PAUSE(element, horiVariables.stepCount, horiVariables.gearRatio);
#ifdef CONFIG_MOTR_DEBUG
		ESP_LOGI(TAG, "processRequestAT | M03 speed is: %f", element);
#endif


		horiVariables.mode = MotorMode::SPINDLE_CLOCKWISE;
		return true;
	}else if (strncmp(gcode, "M04", 3) == 0){ // start spinning horzMot axis counterclockwise TODO: spindle mode should be supported on both motors
		horiVariables.mode = MotorMode::STOPPED;
		horiVariables.stepsToGo = 0;
		element = getElement(3, "S", 1);
		if(element != NAN && element > 0 && element <= CONFIG_MOTR_MAX_SPEED) horiVariables.pause =  RPM_TO_PAUSE(element, horiVariables.stepCount, horiVariables.gearRatio);
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

void MotorControl::setEndstops(int8_t horzMotEndstop, int8_t tiltEndstop){
	if(horzMotEndstop >= 0){
		this->horiVariables.endstop = horzMotEndstop;
	}
	if(tiltEndstop >= 0){
		this->tiltVariables.endstop = tiltEndstop;
	}
}

void MotorControl::tiltEndstopHandler(void *arg){
	MotorControl* instance = (MotorControl*)arg;
	if(instance->tiltVariables.mode == MotorMode::HOMING){
		instance->tiltVariables.mode = MotorMode::STOPPED;
	}

	if(instance->positioningMode == PositioningMode::HOMING_FAST && instance->horiVariables.mode == MotorMode::STOPPED && instance->tiltVariables.mode == MotorMode::STOPPED)
		instance->homeRoutine(1);

	if(instance->positioningMode == PositioningMode::HOMING_SLOW && instance->horiVariables.mode == MotorMode::STOPPED && instance->tiltVariables.mode == MotorMode::STOPPED)
		instance->homeRoutine(2);
}

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
			preHomeHorzPause = horiVariables.pause;
			preHomeTiltPause = tiltVariables.pause;
			preHomePosMode = positioningMode;
			positioningMode = PositioningMode::HOMING_FAST;
			// TODO: register interrupts for both endstops

			tiltVariables.pause =RPM_TO_PAUSE(150, tiltVariables.stepCount, tiltVariables.gearRatio);
			horiVariables.pause =RPM_TO_PAUSE(150, horiVariables.stepCount, horiVariables.gearRatio);
			if(tiltVariables.endstop >= 0){
				tiltVariables.mode = MotorMode::HOMING;
				attachInterruptArg(tiltVariables.endstop, tiltEndstopHandler, this, CHANGE);
			}else
				tiltVariables.mode = MotorMode::STOPPED;


			if(horiVariables.endstop >= 0){
				horiVariables.mode = MotorMode::HOMING;
				attachInterruptArg(horiVariables.endstop, horizontalEndstopHandler, this, CHANGE);
			}else
				horiVariables.mode = MotorMode::STOPPED;

			break; // NOTE: interrupt for both pin will check if both motors have stopped if so it will call homeRoutine(1)
		case 1: // both motors are stopped -> we move them bit

#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "homeRoutine | Fast homing done, moving to slow homing");
#endif
			if(tiltVariables.endstop >= 0){
				tiltVariables.pause = RPM_TO_PAUSE(10, tiltVariables.stepCount, tiltVariables.gearRatio);
				tiltStep(ANGLE_TO_STEP(5, tiltVariables.stepCount, tiltVariables.gearRatio));
			}

			if(horiVariables.endstop >= 0){
				horiVariables.pause = RPM_TO_PAUSE(10, horiVariables.stepCount, horiVariables.gearRatio);
				horiStep(ANGLE_TO_STEP(5, horiVariables.stepCount, horiVariables.gearRatio));
			}

			// activate motor mode again
			tiltVariables.mode = tiltVariables.endstop >=0 ? MotorMode::HOMING : MotorMode::STOPPED;  // this will automatically change behavior in the motorMoveTask
			horiVariables.mode = horiVariables.endstop >=0 ? MotorMode::HOMING : MotorMode::STOPPED;


			break;

		case 2:
#ifdef CONFIG_MOTR_DEBUG
			ESP_LOGI(TAG, "homeRoutine | Motors homed");
#endif
			tiltVariables.angle = 0;
			horiVariables.angle = 0;

			horiVariables.pause = preHomeHorzPause;
			tiltVariables.pause = preHomeTiltPause;
			positioningMode = preHomePosMode;
			tiltVariables.pause = preHomeTiltPause;
			horiVariables.pause = preHomeHorzPause;
			tiltVariables.mode = preHomeTiltMode;
			horiVariables.mode = preHomeHorzMode;

			// TODO: unregister interrupts for both endstops

			break;
	}
	// part one move fast to the endstop
}

void tiltStep(int32_t steps){
	uint32_t stepsLeft = abs(steps);
	uint8_t stepNumber = tiltVariables.stepNumber; // as the function is blocking and designed more for testing and such we don't need atomic operations
	uint64_t lastStepTime  = 0;
	uint64_t now;
	while (steps_left > 0){
		now = esp_timer_get_time();
		if (now - lastStepTime >= tiltVariables.pause){
			lastStepTime = now;
			if (steps < 0){ // XXX check whether these checks are needed or we can just rely on overflow of uint8_t
				stepNumber++:
					if (stepNumber == tiltConfiguration.stepCount)
						stepNumber = 0;
			}
			else{
				if (stepNumber == 0)
					stepNumber = tiltConfiguration.stepCount;
				stepNumber--;
			}
			steps_left--;
			tiltSwitchOutput(stepNumber & 3); // & 3 is the same as % 4 but faster, since modulo requires division

			printf("DIFF: %lld us\n", now-esp_timer_get_time());  // takes ridiculously long time - 22us
		} else
			vTaskDelay(1/portTICK_PERIOD_MS);

	}
	tiltVariables.stepNumber = stepNumber;
}

void horiStep(int32_t steps){
	uint32_t stepsLeft = abs(steps);
	uint8_t stepNumber = horiVariables.stepNumber; // as the function is blocking and designed more for testing and such we don't need atomic operations
	uint64_t lastStepTime  = 0;
	uint64_t now;
	while (steps_left > 0){
		now = esp_timer_get_time();
		if (now - lastStepTime >= horiVariables.pause){
			lastStepTime = now;
			if (steps < 0){ // XXX check whether these checks are needed or we can just rely on overflow of uint8_t
				stepNumber++:
					if (stepNumber == horiConfiguration.stepCount)
						stepNumber = 0;
			}
			else{
				if (stepNumber == 0)
					stepNumber = horiConfiguration.stepCount;
				stepNumber--;
			}
			steps_left--;
			horiSwitchOutput(stepNumber & 3); // & 3 is the same as % 4 but faster, since modulo requires division

			printf("DIFF: %lld us\n", now-esp_timer_get_time());  // takes ridiculously long time - 22us
		} else
			vTaskDelay(1/portTICK_PERIOD_MS);

	}
	horiVariables.stepNumber = stepNumber;

}


void horiSwitchOutput(uint8_t step){

#ifdef CONFIG_MOTR_4WIRE_MODE
	switch (step) {
		case 0:  // 1010
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN3, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN4, LOW);
			break;
		case 1:  // 0110
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN3, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN4, LOW);
			break;
		case 2:  //0101
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN3, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN4, HIGH);
			break;
		case 3:  //1001
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN3, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN4, HIGH);
			break;
	}
#else
	switch (step) {
		case 0:  // 01
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, HIGH);
			break;
		case 1:  // 11
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, HIGH);
			break;
		case 2:  // 10
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, LOW);
			break;
		case 3:  // 00
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_H_PIN2, LOW);
			break;
	}

#endif
}

void tiltSwitchOutput(uint8_t step){

#ifdef CONFIG_MOTR_4WIRE_MODE
	switch (step) {
		case 0:  // 1010
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN3, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN4, LOW);
			break;
		case 1:  // 0110
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN3, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN4, LOW);
			break;
		case 2:  //0101
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN3, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN4, HIGH);
			break;
		case 3:  //1001
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN3, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN4, HIGH);
			break;
	}
#else
	switch (step) {
		case 0:  // 01
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, HIGH);
			break;
		case 1:  // 11
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, HIGH);
			break;
		case 2:  // 10
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, HIGH);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, LOW);
			break;
		case 3:  // 00
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN1, LOW);
			gpio_set_level((gpio_num_t)CONFIG_MOTR_T_PIN2, LOW);
			break;
	}

#endif
}

