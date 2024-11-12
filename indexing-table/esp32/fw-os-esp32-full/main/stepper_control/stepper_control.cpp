/*
 * stepper_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_control.h"

StepperControl stepperControl = StepperControl();

StepperControl::StepperControl() { }

void StepperControl::init()
{

	steppers.initMCPWN();
	steppers.initPCNT();
	StepperControl::homingEventGroup = xEventGroupCreate();

	if (CONFIG_STEPPER_H_PIN_ENDSTOP >= 0) {
		pinMode(CONFIG_STEPPER_H_PIN_ENDSTOP, INPUT);
	}
	if (CONFIG_STEPPER_T_PIN_ENDSTOP >= 0) { // needs external pullup
		pinMode(CONFIG_STEPPER_T_PIN_ENDSTOP, INPUT);
	}

	noProgrammQueueLock = xSemaphoreCreateMutex();
	if (noProgrammQueueLock == NULL) {
		ESP_LOGE(TAG, "JoPkaEndpoint | xSemaphoreCreateMutex failed");
	} else {
		ESP_LOGI(TAG, "JoPkaEndpoint | created lock");
	}
}

uint8_t StepperControl::call(uint16_t id)
{
	return 0;
};

void StepperControl::stepperMoveTask(void* arg)
{ // TODO pin to core 0
	uint64_t time = 0;
	time = esp_timer_get_time();
	while(true){
		if(programmingMode == ProgrammingMode::NO_PROGRAMM){
			if(xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000) == pdTRUE){
				if(noProgrammQueue.size() > 0){
					gcode_command_t command = noProgrammQueue.front();
					noProgrammQueue.pop();
					xSemaphoreGive(noProgrammQueueLock);
				}else{
					xSemaphoreGive(noProgrammQueueLock);
				}
			}

			// non steppers commands are executed immediately
			// stepper commands are added to the queue, if queue is full we sleep the whole task
	}
	vTaskDelay(100000);
	vTaskDelete(NULL);
}

ParsingGCodeResult StepperControl::parseGCode(const char* gcode, uint16_t length)
{

	// lambda that returns number following a given string
	// for example when gcode is "G0 X-1000 S2000" and we search for 'X' lambda returns -1000
	// if element is not found or data following it aren't just numbers NAN is returned
	auto getElementFloat = [gcode, length](uint16_t index, const char* matchString, const uint16_t elementLength) -> float {
		bool negative = false;
		float toReturn = 0;
		uint8_t decimal = 0;
		for (uint16_t i = index; i < length; i++) {
			if (gcode[i] == matchString[0]) {
				if (strncmp(gcode + i, matchString, elementLength) == 0) {
					i += elementLength;

					if (gcode[i] == '-') {
						negative = true;
						i++;
					}
					while (gcode[i] != ' ' && i < length) {
						if (gcode[i] >= '0' && gcode[i] <= '9') {
							toReturn = toReturn * 10 + (gcode[i] - '0');
							if (decimal) {
								decimal++;
							}
							i++;
							continue;
						} else if (gcode[i] == '.' && !decimal) {
							decimal = 1;
							i++;
							continue;
						}
						return GCODE_ELEMENT_INVALID_FLOAT;
					}
					toReturn /= pow(10, decimal - 1);
#ifdef CONFIG_STEPPER_DEBUG
					ESP_LOGI(TAG, "getElement | Found element %s: %f", matchString, negative ? -toReturn : toReturn);
#endif
					return negative ? -toReturn : toReturn;
				}
			}
		}
		return GCODE_ELEMENT_INVALID_FLOAT;
	};

	auto getElementInt = [gcode, length](uint16_t index, const char* matchString, const uint16_t elementLength) -> int64_t {
		bool negative = false;
		int64_t toReturn = 0;
		for (uint16_t i = index; i < length; i++) {
			if (gcode[i] == matchString[0]) {
				if (strncmp(gcode + i, matchString, elementLength) == 0) {
					i += elementLength;
					if (gcode[i] == '-') {
						negative = true;
						i++;
					}
					while (gcode[i] != ' ' && i < length) {
						if (gcode[i] >= '0' && gcode[i] <= '9') {
							toReturn = toReturn * 10 + (gcode[i] - '0');
							i++;
							continue;
						}
						return GCODE_ELEMENT_INVALID_INT;
					}
					return negative ? -toReturn : toReturn;
				}
			}
		}
		return GCODE_ELEMENT_INVALID_INT;
	};

	auto getElementString = [gcode, length](uint16_t index, const char* matchString, const uint16_t elementLength) -> bool {
		for (uint16_t i = index; i < length; i++) {
			if (gcode[i] == matchString[0]) {
				if (strncmp(gcode + i, matchString, elementLength) == 0) {
					return true;
				}
			}
		}
		return false;
	};

	auto getString = [gcode, length](uint16_t index, char* str, int16_t* stringLength) -> bool {
		for (uint16_t i = index; i < length; i++) {
			if (gcode[i] == ' ' || i == length-1)
				return true;

			str[*stringLength] = gcode[i];
			*stringLength++;
		}
		return false;
	};

	// NOTE: all commands will be handled same we aren't in programm regime they will be added to to programm 0
	// all commands should be executed in motorTask -> added to a list and thats all

	if (strncmp(gcode, "M80", 3) == 0) { // power down high voltage supply
																			 // XXX this command will be executed immediately
		return ParsingGCodeResult::NO_SUPPORT;
	} else if (strncmp(gcode, "M81", 3) == 0) { // power up high voltage supply
																							// XXX this command will be executed immediately
		return ParsingGCodeResult::NO_SUPPORT;
	} else if (strncmp(gcode, "P0", 2) == 0) { // stop programm execution
		return ParsingGCodeResult::NO_SUPPORT;
		if (programmingMode != ProgrammingMode::RUN_PROGRAM)
			return ParsingGCodeResult::INVALID_COMMAND;

		if (activeProgram != nullptr) { // we are running a program so reset it and move back to NO_PROGRAMM mode
			activeProgram->reset();
			activeProgram = nullptr;
		}
		programmingMode = ProgrammingMode::NO_PROGRAMM;
	}
	if (programmingMode == ProgrammingMode::RUN_PROGRAM) // all following commands don't make any sense to process if we are already running a program
		return ParsingGCodeResult::NOT_PROCESSING_COMMANDS;

	int64_t elementInt = 0;
	float elementFloat = 0;

	gcode_command_t command;

	switch (gcode[0]) {
	case 'G':
		goto parsingGCodeCommandsG;
	case 'M':
		goto parsingGCodeCommandsM;
	case 'W':
		goto parsingGCodeCommandsW;
	case 'P':
		goto parsingGCodeCommandsP;
	}

parsingGCodeCommandsG:
	if (strncmp(gcode, "G20", 3) == 0) { // set unit to degrees
		command.type = GCodeCommand::G20;
	} else if (strncmp(gcode, "G21", 3) == 0) { // set unit to steps
		command.type = GCodeCommand::G21;
	} else if (strncmp(gcode, "G90", 3) == 0) { // set the absolute positioning
		command.type = GCodeCommand::G90;
	} else if (strncmp(gcode, "G91", 3) == 0) { // set the relative positioning
		command.type = GCodeCommand::G91;
	} else if (strncmp(gcode, "G92", 3) == 0) { // set current position as home
		command.type = GCodeCommand::G92;
	} else if (strncmp(gcode, "G28", 3) == 0) { // home both drivers
		command.type = GCodeCommand::G28;
	} else if (strncmp(gcode, "G0", 2) == 0) { // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
		command.type = GCodeCommand::G0;
		elementInt = getElementInt(3, "H", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.steps = elementInt;
		} else {
			goto endInvalidArgument;
			// return ParsingGCodeResult::INVALID_ARGUMENT; // TODO: check if manual cleanup of pointer isn't needed
		}

		elementInt = getElementInt(3, "T", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command.movementT = new gcode_command_movement_t();
			command.movementT->val.steps = elementInt;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "S", 1);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command.movementH != nullptr)
				command.movementH->rpm = elementFloat;
			if (command.movementT != nullptr)
				command.movementT->rpm = elementFloat;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "SH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command.movementH != nullptr)
				command.movementH->rpm = elementFloat;
			else
				goto endInvalidArgument;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "ST", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command.movementT != nullptr)
				command.movementT->rpm = elementFloat;
			else
				return ParsingGCodeResult::INVALID_COMMAND;
		} else {
			goto endInvalidArgument;
		}
	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	goto endSuccess;

parsingGCodeCommandsM:
	if (strncmp(gcode, "M03", 3) == 0) { // start spinning horzMot axis clockwise
		if (getElementString(3, "H+", 1)) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.direction = Direction::FORWARD;
		} else if (getElementString(3, "H-", 1)) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.direction = Direction::BACKWARD;
		}

		if (getElementString(3, "T+", 1)) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.direction = Direction::FORWARD;
		} else if (getElementString(3, "T-", 1)) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.direction = Direction::BACKWARD;
		}

		elementFloat = getElementFloat(3, "S", 1);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command.movementH != nullptr)
				command.movementH->rpm = elementFloat;
			if (command.movementT != nullptr)
				command.movementT->rpm = elementFloat;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "SH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command.movementH != nullptr)
				command.movementH->rpm = elementFloat;
			else
				goto endInvalidArgument;

		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "ST", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command.movementT != nullptr)
				command.movementT->rpm = elementFloat;
			else
				goto endInvalidArgument;
		} else {
			goto endInvalidArgument;
		}
	} else if (strncmp(gcode, "M05", 3) == 0) {
		command.type = GCodeCommand::M05;
		if (getElementString(3, "H", 1)) {
			command.movementH = new gcode_command_movement_t(); // stepper will stop in command.movementT is not nullptr
		} else
			goto endInvalidArgument;

		if (getElementString(3, "T", 1)) {
			command.movementT = new gcode_command_movement_t(); // stepper will stop in command.movementT is not nullptr
		} else
			goto endInvalidArgument;

	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	goto endSuccess;

parsingGCodeCommandsW:
	if (strncmp(gcode, "W0", 2) == 0) {
		command.type = GCodeCommand::W1; // all W0 command futhers on will be handled as W1
		elementInt = getElementInt(2, "H", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.time = elementInt * 1000;
		} else
			goto endInvalidArgument;

		elementInt = getElementInt(2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command.movementT = new gcode_command_movement_t();
			command.movementT->val.time = elementInt * 1000;
		} else
			goto endInvalidArgument;

	} else if (strncmp(gcode, "W1", 2) == 0) {
		command.type = GCodeCommand::W1;
		elementInt = getElementInt(2, "H", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command.movementH = new gcode_command_movement_t();
			command.movementH->val.time = elementInt;
		} else
			goto endInvalidArgument;

		elementInt = getElementInt(2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command.movementT = new gcode_command_movement_t();
			command.movementT->val.time = elementInt;
		} else
			goto endInvalidArgument;

	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	goto endSuccess;
parsingGCodeCommandsP:
	if(strncmp(gcode, "P1", 2)){
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::NOT_PROCESSING_COMMANDS; // TODO: make sure this never arises

		char name[20];
		uint16_t nameLength = 0;
		bool out = getString(3, name, &nameLength);
		if(!out) return ParsingGCodeResult::INVALID_ARGUMENT;

		//switch programmingMode, switch activeProgram
		for (auto it = programms.begin(); it != programms.end(); it++)
			if (strncmp(it->name, name, 20) == 0)
				break;

		if (it != programms.end()){
			activeProgram = &(*it);
			activeProgram->reset(); // we never do cleanup on program end
			programmingMode = ProgrammingMode::RUN_PROGRAM;
			return ParsingGCodeResult::SUCCESS;
		}else
			return ParsingGCodeResult::INVALID_ARGUMENT;

	}else if(strncmp(gcode, "P2", 2)){
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::NOT_PROCESSING_COMMANDS; // TODO: make sure this never arises

		char name[20];
		uint16_t nameLength = 0;
		bool out = getString(3, name, &nameLength);
		if(!out) return ParsingGCodeResult::INVALID_ARGUMENT;

		for (auto it = programms.begin(); it != programms.end(); it++)
			if (strncmp(it->name, name, 20) == 0)
				break;

		if (it != programms.end())
			programms.erase(it);

		return ParsingGCodeResult::SUCCESS;


	}else if (strncmp(gcode, "P90", 3)) {
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::CODE_FAILURE; // TODO: make sure this never arises

		char name[20];
		uint16_t nameLength = 0;
		bool out = getString(4, name, &nameLength);
		if(!out) return ParsingGCodeResult::INVALID_ARGUMENT;

		for (auto it = programms.begin(); it != programms.end(); it++)
			if (strncmp(it->name, name, 20) == 0)
				break;
		if (it != programms.end()) { // programm with the same name exists so we will overwrite it
			// -> if there is a program with the same name we will overwrite it
			activeProgram = &(*it);
			activeProgram->clean();
			commandDestination = activeProgram->header;
		} else { // create new programm
			gcode_programm_t newProgram;
			strncpy(newProgram.name, name, 20);
			programms.push_back(newProgram);
			activeProgram = &(programms.back());
			commandDestination = activeProgram->header;
		}
		programmingMode = ProgrammingMode::PROGRAMMING;
		return ParsingGCodeResult::SUCCESS;
	}
	if (programmingMode != ProgrammingMode::PROGRAMMING)
		return ParsingGCodeResult::INVALID_COMMAND;

	if (strncmp(gcode, "P91", 3)) {

		if (commandDestination != activeProgram->header)
			return ParsingGCodeResult::INVALID_COMMAND;

		commandDestination = activeProgram->main;
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "P21", 3)) {
		command.type = GCodeCommand::P21;
		activeProgram->forLoopCounter++;
	} else if (strncmp(gcode, "P22", 3)) {
		command.type = GCodeCommand::P22;
		activeProgram->forLoopCounter--;
	} else if (strncmp(gcode, "P29", 3)) {
		if (commandDestination != activeProgram->header) // we can only declare looped command in the header
			return ParsingGCodeResult::INVALID_COMMAND;
		activeProgram->repeatIndefinitely = true;
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "P92", 3)) {
		if (activeProgram->forLoopCounter != 0) {
			return ParsingGCodeResult::NON_CLOSED_LOOP;
		} else {
			activeProgram->reset();
			activeProgram = nullptr;
		}

		programmingMode = ProgrammingMode::NO_PROGRAMM;

		return ParsingGCodeResult::SUCCESS;
	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	goto endSuccess;

endSuccess:
	if (programmingMode == ProgrammingMode::NO_PROGRAMM) {
		if (xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000) != pdTRUE) {
			noProgrammQueue.push(command);
			xSemaphoreGive(noProgrammQueueLock);
			return ParsingGCodeResult::SUCCESS;
		} else
			goto endFailedToLockQueue;
	} else {
		if(commandDestination == activeProgram->main && !(command.type & 0b1000'0000))
			goto endCommandNotAllowed;
		commandDestination->push_back(command);
		return ParsingGCodeResult::SUCCESS;
	}

endFailedToLockQueue:
	if (command.movementH != nullptr)
		delete command.movementH;
	if (command.movementT != nullptr)
		delete command.movementT;

	return ParsingGCodeResult::FAILED_TO_LOCK_QUEUE;

endInvalidArgument:
	if (command.movementH != nullptr) {
		delete command.movementH;
	}
	if (command.movementT != nullptr)
		delete command.movementT;
	return ParsingGCodeResult::INVALID_ARGUMENT;

endCommandNotAllowed:
	if (command.movementH != nullptr) {
		delete command.movementH;
	}
	if (command.movementT != nullptr)
		delete command.movementT;
	return ParsingGCodeResult::COMMAND_NOT_ALLOWED;
}

void StepperControl::tiltEndstopHandler(void* arg)
{
	if (stepperControl.programmingMode == ProgrammingMode::HOMING) {
		steppers.stopStepperH();
		xEventGroupSetBits(StepperControl::homingEventGroup, STEPPER_COMPLETE_BIT_H);
	}
}

void StepperControl::horizontalEndstopHandler(void* arg)
{
	if (stepperControl.programmingMode == ProgrammingMode::HOMING) {
		steppers.stopStepperT();
		xEventGroupSetBits(StepperControl::homingEventGroup, STEPPER_COMPLETE_BIT_T);
	}
}

void StepperControl::home()
{
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
			portMAX_DELAY);

	if (result & STEPPER_COMPLETE_BIT_H) {
		ESP_LOGI(TAG, "Home | Horizontal stepper fast homed");
	}
	if (result & STEPPER_COMPLETE_BIT_T) {
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
			portMAX_DELAY);

	if (result & STEPPER_COMPLETE_BIT_H) {
		ESP_LOGI(TAG, "Home | Horizontal stepper slow homed");
	}
	if (result & STEPPER_COMPLETE_BIT_T) {
		ESP_LOGI(TAG, "Home | Tilt stepper slow homed");
	}

	varsH.stepNumber.store(0);
	varsT.stepNumber.store(0);

	// cleanup
	xEventGroupClearBits(homingEventGroup, HOMING_DONE_BIT);
	detachInterrupt(CONFIG_STEPPER_H_PIN_ENDSTOP);
	detachInterrupt(CONFIG_STEPPER_T_PIN_ENDSTOP);
}
