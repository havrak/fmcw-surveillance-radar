/*
 * stepper_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "stepper_control.h"

StepperControl stepperControl = StepperControl();

StepperControl::StepperControl() { }

std::queue<gcode_command_t*> StepperControl::noProgrammQueue = std::queue<gcode_command_t*>();
SemaphoreHandle_t StepperControl::noProgrammQueueLock = xSemaphoreCreateBinary();

void StepperControl::init()
{

	steppers.initMCPWN();
	steppers.initPCNT();
	steppers.initStepperTasks();
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
	// on first step there are some initializations taking place that make it much longer than the rest
	steppers.stepStepperH(1, 30);
	steppers.stepStepperH(-1, 30);
	steppers.stepStepperT(1, 30);
	steppers.stepStepperT(-1, 30);
	steppers.waitStepperH(10, false);
	steppers.waitStepperT(10, false);

}

void StepperControl::stepperMoveTask(void* arg)
{ // TODO pin to core 0
	uint64_t time = 0;
	time = esp_timer_get_time();

	stepper_variables_t varsH;
	stepper_variables_t varsT;
	int32_t positionTempH = 0;
	int32_t positionTempT = 0;
	int32_t stepsTemp = 0;
	gcode_command_t* command = (gcode_command_t*)0x1; // NOTE: this is just hack so I don't have tu turn off -Werror=maybe-uninitialized

	// checks order of from and dest when in union of intervals <0, min> U <max, stepCount>
	// true if from comes before dest -> we need to move clockwise

	auto moveStepperAbsolute = [command](bool (StepperHal::*stepFunction)(int16_t, float, bool), uint16_t stepCount, const gcode_command_movement_t* movement, const stepper_variables_t* vars, int32_t dest) -> int32_t {
		if (vars->stepsMax == GCODE_ELEMENT_INVALID_INT32 && vars->stepsMin == GCODE_ELEMENT_INVALID_INT32)
			(steppers.*stepFunction)(ANGLE_DISTANCE(vars->positionLastScheduled, dest, stepCount), movement->rpm, SYNCHRONIZED);
		else if (vars->stepsMax >= vars->stepsMin) { // moving in interval <min, max>
			if (dest > vars->stepsMax)
				dest = vars->stepsMax;
			else if (dest < vars->stepsMin)
				dest = vars->stepsMin;

			// now we need to pick from clockwise or counterclockwise rotation, where one can possible go outside of the limits
			if (vars->positionLastScheduled < dest)
				(steppers.*stepFunction)(ANGLE_DISTANCE_CLOCKWISE(vars->positionLastScheduled, dest, stepCount), movement->rpm, SYNCHRONIZED);
			else
				(steppers.*stepFunction)(ANGLE_DISTANCE_COUNTERCLOCKWISE(vars->positionLastScheduled, dest, stepCount), movement->rpm, SYNCHRONIZED);
		} else { // moving in interval <0, max> U <min, stepCount>
			uint32_t avg = (vars->stepsMax + vars->stepsMin) / 2;
			if (dest >= avg && dest < vars->stepsMin)
				dest = vars->stepsMin;
			else if (dest < avg && dest > vars->stepsMax)
				dest = vars->stepsMax;

			bool fromInInterval1 = (vars->positionLastScheduled >= 0 && vars->positionLastScheduled <= vars->stepsMax);
			bool fromInInterval2 = (vars->positionLastScheduled >= vars->stepsMin && vars->positionLastScheduled <= stepCount);
			bool destInInterval1 = (dest >= 0 && dest <= vars->stepsMax);
			bool destInInterval2 = (dest >= vars->stepsMin && dest <= stepCount);

			bool order = false;

			if (fromInInterval1 && destInInterval1)
				order = vars->positionLastScheduled < dest;
			else if (fromInInterval2 && destInInterval2)
				order = vars->positionLastScheduled < dest;
			else if (fromInInterval1 && destInInterval2)
				order = true;
			else if (fromInInterval2 && destInInterval1)
				order = true;

			// we need to calculete in which order steps and positionLastScheduled are
			if (order)
				(steppers.*stepFunction)(ANGLE_DISTANCE_CLOCKWISE(vars->positionLastScheduled, dest, stepCount), movement->rpm, SYNCHRONIZED);
			else
				(steppers.*stepFunction)(ANGLE_DISTANCE_CLOCKWISE(vars->positionLastScheduled, dest, stepCount), movement->rpm, SYNCHRONIZED);
		}
		return dest;
	};

	auto moveStepperRelative = [command](bool (StepperHal::*stepFunction)(int16_t, float, bool), uint16_t stepCount, const gcode_command_movement_t* movement, const stepper_variables_t* vars, int32_t steps) -> int32_t {
		if (vars->stepsMax == GCODE_ELEMENT_INVALID_INT32 && vars->stepsMin == GCODE_ELEMENT_INVALID_INT32) {
			(steppers.*stepFunction)(steps, movement->rpm, SYNCHRONIZED);
		} else if (vars->stepsMax >= vars->stepsMin) { // moving in interval <min, max>
			if (vars->positionLastScheduled + steps >= vars->stepsMax)
				steps = vars->stepsMax - vars->positionLastScheduled;
			else if (vars->positionLastScheduled + steps <= vars->stepsMin)
				steps = vars->stepsMin - vars->positionLastScheduled;

			(steppers.*stepFunction)(steps, movement->rpm, SYNCHRONIZED);
		} else { // moving in interval <0, max> U <min, stepCount>

				int16_t maxStepsCCW = (vars->positionLastScheduled >= vars->stepsMin) ? -(vars->positionLastScheduled - vars->stepsMin) : -(vars->positionLastScheduled + stepCount - vars->stepsMin);
				// Calculate vars->stepsMaximum allowable steps clockwise (positive)
				int16_t maxStepsCW = (vars->positionLastScheduled <= vars->stepsMax) ? (vars->stepsMax - vars->positionLastScheduled) : (stepCount - vars->positionLastScheduled + vars->stepsMax);
				if (steps < maxStepsCCW)
						steps = maxStepsCCW;
				else if (steps > maxStepsCW)
						steps = maxStepsCW;



			(steppers.*stepFunction)(steps, movement->rpm, SYNCHRONIZED);
		}
		return NORMALIZE_ANGLE(vars->positionLastScheduled + steps, stepCount);
	};

	Unit unit = Unit::DEGREES; // only used in motorTask

	while (true) {
		varsH.position += steppers.getStepsTraveledOfPrevCommandH();
		varsT.position += steppers.getStepsTraveledOfPrevCommandT();

		printf("!P %lld, %f, %f\n", esp_timer_get_time(), STEPS_TO_ANGLE(NORMALIZE_ANGLE(varsH.position + steppers.getStepsTraveledOfCurrentCommandH(), CONFIG_STEPPER_H_STEP_COUNT), CONFIG_STEPPER_H_STEP_COUNT), STEPS_TO_ANGLE(NORMALIZE_ANGLE(varsT.position + steppers.getStepsTraveledOfCurrentCommandT(), CONFIG_STEPPER_H_STEP_COUNT), CONFIG_STEPPER_H_STEP_COUNT));

		// if queues are filled we will wait
if(steppers.getQueueLengthH() ==  CONFIG_STEPPER_HAL_QUEUE_SIZE || steppers.getQueueLengthT() ==  CONFIG_STEPPER_HAL_QUEUE_SIZE){
			vTaskDelay(20 / portTICK_PERIOD_MS);
			continue;
		}

		if (programmingMode == ProgrammingMode::NO_PROGRAMM) {
			if (xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000) == pdTRUE) {
				if (noProgrammQueue.size() > 0) {
					command = noProgrammQueue.front();
					noProgrammQueue.pop();
				}
				xSemaphoreGive(noProgrammQueueLock);
			}
		} else if (programmingMode == ProgrammingMode::RUN_PROGRAM) {
			if (activeProgram->indexHeader != activeProgram->header->size()) {
				command = activeProgram->header->at(activeProgram->indexHeader);
				activeProgram->indexHeader++;
			} else if (activeProgram->indexMain == activeProgram->main->size()) {
				command = activeProgram->main->at(activeProgram->indexMain);
				activeProgram->indexMain++;
			} else if (activeProgram->repeatIndefinitely) {
				activeProgram->indexMain = 0;
			} else {
				programmingMode.store(ProgrammingMode::NO_PROGRAMM);
				continue;
			}
		} else {
			vTaskDelay(20 / portTICK_PERIOD_MS); // there are no commands to process, we can wait and will only refresh the position
			continue;
		}

		// reporting will be always is always in absolute position
		switch (command->type) {
		case GCodeCommand::G20:
			unit = Unit::DEGREES;
			break;
		case GCodeCommand::G21:
			unit = Unit::STEPS;
			break;
		case GCodeCommand::G90:
			if (command->movementH != nullptr)
				varsH.positioningMode = PositioningMode::ABSOLUTE;
			if (command->movementT != nullptr)
				varsT.positioningMode = PositioningMode::ABSOLUTE;
			break;
		case GCodeCommand::G91:
			if (command->movementH != nullptr)
				varsH.positioningMode = PositioningMode::RELATIVE;
			if (command->movementT != nullptr)
				varsT.positioningMode = PositioningMode::RELATIVE;
			break;
		case GCodeCommand::G92:
			if (steppers.getQueueLengthH() == 0 && steppers.getQueueLengthT() == 0) {
				steppers.getStepsTraveledOfPrevCommandH(); // clear previous command steps
				steppers.getStepsTraveledOfPrevCommandT();

				varsH.position = 0;
				varsT.position = 0;
				varsH.positionLastScheduled = 0;
				varsT.positionLastScheduled = 0;
			}
			break;
		case GCodeCommand::G28:
			stepperControl.home();
			break;
		case GCodeCommand::G0:
			if (command->movementH != nullptr) {
				if(command->movementH->val.steps == 0)
					steppers.skipStepperH(SYNCHRONIZED);
				stepsTemp = unit == Unit::DEGREES ? ANGLE_TO_STEPS(command->movementH->val.steps, CONFIG_STEPPER_H_STEP_COUNT) : command->movementH->val.steps;
				if (varsH.positioningMode == PositioningMode::ABSOLUTE) {
					stepsTemp = NORMALIZE_ANGLE(stepsTemp, CONFIG_STEPPER_H_STEP_COUNT);
					varsH.positionLastScheduled = moveStepperAbsolute(&StepperHal::stepStepperH, CONFIG_STEPPER_H_STEP_COUNT, command->movementH, &varsH, stepsTemp);
				} else {
					varsH.positionLastScheduled = moveStepperRelative(&StepperHal::stepStepperH, CONFIG_STEPPER_H_STEP_COUNT, command->movementH, &varsH, stepsTemp);
				}
			}
			if (command->movementT != nullptr) {
				if(command->movementT->val.steps == 0)
					steppers.skipStepperT(SYNCHRONIZED);
				stepsTemp = unit == Unit::DEGREES ? ANGLE_TO_STEPS(command->movementT->val.steps, CONFIG_STEPPER_T_STEP_COUNT) : command->movementT->val.steps;
				if (varsT.positioningMode == PositioningMode::ABSOLUTE) {
					stepsTemp = NORMALIZE_ANGLE(stepsTemp, CONFIG_STEPPER_T_STEP_COUNT);
					varsT.positionLastScheduled = moveStepperAbsolute(&StepperHal::stepStepperT, CONFIG_STEPPER_T_STEP_COUNT, command->movementT, &varsT, stepsTemp);
				} else {
					varsT.positionLastScheduled = moveStepperRelative(&StepperHal::stepStepperT, CONFIG_STEPPER_T_STEP_COUNT, command->movementT, &varsT, stepsTemp);
				}
			}
			break;
		case GCodeCommand::M03:
			if (command->movementH != nullptr) {
				if (varsH.positioningMode != PositioningMode::RELATIVE) {
					ESP_LOGE(TAG, "ERR: command with absolute positioning is not supported, will switch to relative");
					varsH.positioningMode = PositioningMode::RELATIVE;
				}
				steppers.spindleStepperH(command->movementH->rpm, command->movementH->val.direction);
			}
			if (command->movementT != nullptr) {
				if (varsT.positioningMode != PositioningMode::RELATIVE) {
					ESP_LOGE(TAG, "ERR: command with absolute positioning is not supported, will switch to relative");
					varsT.positioningMode = PositioningMode::RELATIVE;
				}
				steppers.spindleStepperT(command->movementT->rpm, command->movementT->val.direction);
			}
			break;
		case GCodeCommand::M05:
			if (command->movementH != nullptr)
				steppers.stopStepperH(SYNCHRONIZED);
			if (command->movementT != nullptr)
				steppers.stopStepperT(SYNCHRONIZED);
			break;
		case GCodeCommand::M201:
			if (unit == Unit::STEPS) {
				if (command->movementH != nullptr) {
					varsH.stepsMin = command->movementH->val.limits.min <= CONFIG_STEPPER_H_STEP_COUNT ? command->movementH->val.limits.min : CONFIG_STEPPER_H_STEP_COUNT;
					varsH.stepsMax = command->movementH->val.limits.max <= CONFIG_STEPPER_H_STEP_COUNT ? command->movementH->val.limits.max : CONFIG_STEPPER_H_STEP_COUNT;
				}
				if (command->movementT != nullptr) {
					varsT.stepsMin = command->movementT->val.limits.min <= CONFIG_STEPPER_T_STEP_COUNT ? command->movementT->val.limits.min : CONFIG_STEPPER_T_STEP_COUNT;
					varsT.stepsMax = command->movementT->val.limits.max <= CONFIG_STEPPER_T_STEP_COUNT ? command->movementT->val.limits.max : CONFIG_STEPPER_T_STEP_COUNT;
				}
			} else {
				if (command->movementH != nullptr) {
					varsH.stepsMin = ANGLE_TO_STEPS(command->movementH->val.limits.min, CONFIG_STEPPER_H_PIN_ENDSTOP) <= CONFIG_STEPPER_H_STEP_COUNT ? ANGLE_TO_STEPS(command->movementH->val.limits.min, CONFIG_STEPPER_H_PIN_ENDSTOP) : 360;
					varsH.stepsMax = ANGLE_TO_STEPS(command->movementH->val.limits.max, CONFIG_STEPPER_H_PIN_ENDSTOP) <= CONFIG_STEPPER_H_STEP_COUNT ? ANGLE_TO_STEPS(command->movementH->val.limits.max, CONFIG_STEPPER_H_PIN_ENDSTOP) : 360;
				}
				if (command->movementT != nullptr) {
					varsT.stepsMin = ANGLE_TO_STEPS(command->movementT->val.limits.min, CONFIG_STEPPER_T_PIN_ENDSTOP) <= CONFIG_STEPPER_T_STEP_COUNT ? ANGLE_TO_STEPS(command->movementT->val.limits.min, CONFIG_STEPPER_T_PIN_ENDSTOP) : 360;
					varsT.stepsMax = ANGLE_TO_STEPS(command->movementT->val.limits.max, CONFIG_STEPPER_T_PIN_ENDSTOP) <= CONFIG_STEPPER_T_STEP_COUNT ? ANGLE_TO_STEPS(command->movementT->val.limits.max, CONFIG_STEPPER_T_PIN_ENDSTOP) : 360;
				}
			}
			break;

		case GCodeCommand::M202:
			if (command->movementH != nullptr) {
				varsH.stepsMin = GCODE_ELEMENT_INVALID_INT32;
				varsH.stepsMax = GCODE_ELEMENT_INVALID_INT32;
			}
			if (command->movementT != nullptr) {
				varsT.stepsMin = GCODE_ELEMENT_INVALID_INT32;
				varsT.stepsMax = GCODE_ELEMENT_INVALID_INT32;
			}
			break;
		case GCodeCommand::P21:
			activeProgram->forLoopCounter = command->movementH->val.iterations;
			if (activeProgram->indexHeader != activeProgram->header->size())
				activeProgram->indexForLoop = activeProgram->indexHeader + 1;
			else
				activeProgram->indexForLoop = activeProgram->indexMain + 1;

			break;
		case GCodeCommand::P22:
			if (activeProgram->forLoopCounter > 0) {
				activeProgram->forLoopCounter--;
				if (activeProgram->indexHeader != activeProgram->header->size())
					activeProgram->indexHeader = activeProgram->indexForLoop;
				else
					activeProgram->indexMain = activeProgram->indexForLoop;
			}
			break;
		case GCodeCommand::W1:
			if (command->movementH != nullptr) {
				steppers.waitStepperH(command->movementH->val.time, SYNCHRONIZED);
			}
			if (command->movementT != nullptr) {
				steppers.waitStepperT(command->movementT->val.time, SYNCHRONIZED);
			}
			break;
		default:
			break;
		}
		if (programmingMode == ProgrammingMode::NO_PROGRAMM) // WARN: if we ever device that it is possible to switch programm mode in motorTask this check will be invalid
			delete command;
	}
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
					// #ifdef CONFIG_STEPPER_DEBUG
					// 					ESP_LOGI(TAG, "getElement | Found element %s: %f", matchString, negative ? -toReturn : toReturn);
					// #endif
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

	auto getString = [gcode, length](uint16_t index, char* str, uint16_t* stringLength) -> bool {
		for (uint16_t i = index; i < length; i++) {
			if (gcode[i] == ' ' || i == length - 1)
				return true;

			str[*stringLength] = gcode[i];
			(*stringLength)++;
		}
		return false;
	};
	int64_t elementInt = 0;
	float elementFloat = 0;


	// NOTE: all commands will be handled same we aren't in programm regime they will be added to to programm 0
	// all commands should be executed in motorTask -> added to a list and thats all

	if (strncmp(gcode, "M80", 3) == 0) { // power down high voltage supply
																			 // XXX this command will be executed immediately

		activeProgram = nullptr;
		programmingMode.store(ProgrammingMode::NO_PROGRAMM);
		xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000);
		noProgrammQueue = {};
		xSemaphoreGive(noProgrammQueueLock);
		steppers.stopNowStepperH();
		steppers.stopNowStepperT();

		return ParsingGCodeResult::NO_SUPPORT;
	} else if (strncmp(gcode, "M81", 3) == 0) { // power up high voltage supply
																							// XXX this command will be executed immediately
		return ParsingGCodeResult::NO_SUPPORT;
	} else if (strncmp(gcode, "M82", 3) == 0) { // carefull stop
		activeProgram = nullptr;
		programmingMode.store(ProgrammingMode::NO_PROGRAMM);
		xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000);
		noProgrammQueue = {};
		xSemaphoreGive(noProgrammQueueLock);
		steppers.carefullStop();
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "G3", 2) == 0) { // override

		gcode_command_movement_t* movementH = nullptr;
		gcode_command_movement_t* movementT = nullptr;
		elementInt = getElementInt(3, "H", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			movementH = new gcode_command_movement_t();
			movementH->val.steps = elementInt;
		} else
			return ParsingGCodeResult::INVALID_ARGUMENT;


		elementInt = getElementInt(3, "T", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			movementT = new gcode_command_movement_t();
			movementT->val.steps = elementInt;
		} else
			return ParsingGCodeResult::INVALID_ARGUMENT;


		elementFloat = getElementFloat(3, "S", 1);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (movementH != nullptr)
				movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			if (movementT != nullptr)
				movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		} else
			return ParsingGCodeResult::INVALID_ARGUMENT;


		elementFloat = getElementFloat(3, "SH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && movementH != nullptr)
			movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		else
			return ParsingGCodeResult::INVALID_ARGUMENT;

		elementFloat = getElementFloat(3, "ST", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && movementT != nullptr)
			movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		else
			return ParsingGCodeResult::INVALID_ARGUMENT;


		if(movementH !=nullptr && movementT != nullptr){
			steppers.stepStepperH(movementH->val.steps, movementH->rpm, true);
			steppers.stepStepperT(movementT->val.steps, movementT->rpm, true);
			return ParsingGCodeResult::SUCCESS;
		}
		if(movementH !=nullptr)
			steppers.stepStepperH(movementH->val.steps, movementH->rpm, true);
		if(movementT !=nullptr)
			steppers.stepStepperT(movementT->val.steps, movementT->rpm, true);
		return ParsingGCodeResult::SUCCESS;

	} else if (strncmp(gcode, "P0", 2) == 0) { // stop programm execution
		return ParsingGCodeResult::NO_SUPPORT;
		if (programmingMode != ProgrammingMode::RUN_PROGRAM)
			return ParsingGCodeResult::INVALID_COMMAND;

		if (activeProgram != nullptr) { // we are running a program so reset it and move back to NO_PROGRAMM mode
			activeProgram->reset();
			activeProgram = nullptr;
		}
		programmingMode.store(ProgrammingMode::NO_PROGRAMM);
		return ParsingGCodeResult::SUCCESS;
	}

	if (programmingMode == ProgrammingMode::RUN_PROGRAM) // all following commands don't make any sense to process if we are already running a program
		return ParsingGCodeResult::NOT_PROCESSING_COMMANDS;


	gcode_command_t* command = new gcode_command_t();
	auto deleteCommand = [command]() {
		if (command->movementH != nullptr)
			delete command->movementH;
		if (command->movementT != nullptr)
			delete command->movementT;
		if (command != nullptr)
			delete command;
	};

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
		command->type = GCodeCommand::G20;
	} else if (strncmp(gcode, "G21", 3) == 0) { // set unit to steps
		command->type = GCodeCommand::G21;
	} else if (strncmp(gcode, "G90", 3) == 0) { // set the absolute positioning
		command->type = GCodeCommand::G90;
		if (getElementString(3, "H", 1))
			command->movementH = new gcode_command_movement_t();
		if (getElementString(3, "T", 1))
			command->movementT = new gcode_command_movement_t();
	} else if (strncmp(gcode, "G91", 3) == 0) { // set the relative positioning
		command->type = GCodeCommand::G91;
		if (getElementString(3, "H", 1))
			command->movementH = new gcode_command_movement_t();
		if (getElementString(3, "T", 1))
			command->movementT = new gcode_command_movement_t();
	} else if (strncmp(gcode, "G92", 3) == 0) { // set current position as home
		command->type = GCodeCommand::G92;
	} else if (strncmp(gcode, "G28", 3) == 0) { // home both drivers
		command->type = GCodeCommand::G28;
	} else if (strncmp(gcode, "G0", 2) == 0) { // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
		command->type = GCodeCommand::G0;
		elementInt = getElementInt(3, "H", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.steps = elementInt;
		} else {
			goto endInvalidArgument;
			// return ParsingGCodeResult::INVALID_ARGUMENT; // TODO: check if manual cleanup of pointer isn't needed
		}

		elementInt = getElementInt(3, "T", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.steps = elementInt;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "S", 1);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command->movementH != nullptr)
				command->movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			if (command->movementT != nullptr)
				command->movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "SH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && command->movementH != nullptr)
			command->movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		else
			goto endInvalidArgument;

		elementFloat = getElementFloat(3, "ST", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && command->movementT != nullptr)
			command->movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		else
			goto endInvalidArgument;

	} else
		goto endInvalidCommand;

	goto endSuccess;

parsingGCodeCommandsM:
	if (strncmp(gcode, "M03", 3) == 0) { // start spinning horzMot axis clockwise
		if (getElementString(3, "H+", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::FORWARD;
		} else if (getElementString(3, "H-", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::BACKWARD;
		}

		if (getElementString(3, "T+", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::FORWARD;
		} else if (getElementString(3, "T-", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::BACKWARD;
		}

		elementFloat = getElementFloat(3, "S", 1);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command->movementH != nullptr)
				command->movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			if (command->movementT != nullptr)
				command->movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "SH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command->movementH != nullptr)
				command->movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			else
				goto endInvalidArgument;

		} else {
			goto endInvalidArgument;
		}

		elementFloat = getElementFloat(3, "ST", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT) {
			if (command->movementT != nullptr)
				command->movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			else
				goto endInvalidArgument;
		} else {
			goto endInvalidArgument;
		}
	} else if (strncmp(gcode, "M05", 3) == 0) {
		command->type = GCodeCommand::M05;
		if (getElementString(3, "H", 1)) {
			command->movementH = new gcode_command_movement_t(); // stepper will stop in command->movementT is not nullptr
		} else
			goto endInvalidArgument;

		if (getElementString(3, "T", 1)) {
			command->movementT = new gcode_command_movement_t(); // stepper will stop in command->movementT is not nullptr
		} else
			goto endInvalidArgument;
	} else if (strncmp(gcode, "M201", 4) == 0) {
		elementFloat = getElementFloat(5, "LH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && elementFloat >= 0) {
			command->movementH = new gcode_command_movement_t(); // stepper will stop in command->movementT is not nullptr
			command->movementH->val.limits.min = elementFloat;
		} else
			goto endInvalidArgument;

		elementFloat = getElementFloat(5, "HH", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && elementFloat >= 0) {
			if (command->movementH == nullptr)
				command->movementH = new gcode_command_movement_t();
			command->movementH->val.limits.max = elementFloat;
		} else
			goto endInvalidArgument;

		elementFloat = getElementFloat(5, "LT", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && elementFloat >= 0) {
			command->movementT = new gcode_command_movement_t(); // stepper will stop in command->movementT is not nullptr
			command->movementT->val.limits.min = elementFloat;
		} else
			goto endInvalidArgument;

		elementFloat = getElementFloat(5, "HT", 2);
		if (elementFloat != GCODE_ELEMENT_INVALID_FLOAT && elementFloat >= 0) {
			if (command->movementT == nullptr)
				command->movementT = new gcode_command_movement_t();
			command->movementT->val.limits.max = elementFloat;
		} else
			goto endInvalidArgument;
	} else if (strncmp(gcode, "M201", 4) == 0) {
		if (getElementString(5, "H", 1)) {
			command->movementH = new gcode_command_movement_t();
		}
		if (getElementString(5, "T", 1)) {
			command->movementT = new gcode_command_movement_t();
		}
	} else
		goto endInvalidCommand;

	goto endSuccess;

parsingGCodeCommandsW:
	if (strncmp(gcode, "W0", 2) == 0) {
		command->type = GCodeCommand::W1; // all W0 command futhers on will be handled as W1
		elementInt = getElementInt(2, "H", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.time = elementInt * 1000;
		} else
			goto endInvalidArgument;

		elementInt = getElementInt(2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.time = elementInt * 1000;
		} else
			goto endInvalidArgument;
	} else if (strncmp(gcode, "W1", 2) == 0) {
		command->type = GCodeCommand::W1;
		elementInt = getElementInt(2, "H", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.time = elementInt;
		} else
			goto endInvalidArgument;

		elementInt = getElementInt(2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.time = elementInt;
		} else
			goto endInvalidArgument;
	} else
		goto endInvalidCommand;

	goto endSuccess;
parsingGCodeCommandsP:
	if (strncmp(gcode, "P21", 3)) {
		if (programmingMode != ProgrammingMode::PROGRAMMING)
			goto endBadContext;
		command->type = GCodeCommand::P21;
		if (activeProgram->forLoopCounter != 0) {
			deleteCommand();
			return ParsingGCodeResult::NON_CLOSED_LOOP;
		}
		activeProgram->forLoopCounter++;

		elementInt = getElementInt(3, "I", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.iterations = elementInt;
		} else
			goto endInvalidArgument;
		goto endSuccess;
	} else if (strncmp(gcode, "P22", 3)) {
		if (programmingMode != ProgrammingMode::PROGRAMMING)
			goto endBadContext;
		command->type = GCodeCommand::P22;
		activeProgram->forLoopCounter--;
		goto endSuccess;
	}

	delete command; // all commands futher on are processed here and we don't need to store them

	if (strncmp(gcode, "P1", 2)) {
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::NOT_PROCESSING_COMMANDS; // TODO: make sure this never arises

		char name[20];
		uint16_t nameLength = 0;
		bool out = getString(3, name, &nameLength);
		if (!out)
			return ParsingGCodeResult::INVALID_ARGUMENT;

		// switch programmingMode, switch activeProgram
		auto it = programms.begin();
		for (; it != programms.end(); it++)
			if (strncmp(it->name, name, 20) == 0)
				break;

		if (it != programms.end()) {
			activeProgram = &(*it);
			activeProgram->reset(); // we never do cleanup on program end
			programmingMode.store(ProgrammingMode::RUN_PROGRAM);
			xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000);
			noProgrammQueue = {};
			xSemaphoreGive(noProgrammQueueLock);
			steppers.stopNowStepperH();
			steppers.stopNowStepperT();

			return ParsingGCodeResult::SUCCESS;
		} else
			return ParsingGCodeResult::INVALID_ARGUMENT;
	} else if (strncmp(gcode, "P2", 2)) {
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::NOT_PROCESSING_COMMANDS; // TODO: make sure this never arises

		char name[20];
		uint16_t nameLength = 0;
		bool out = getString(3, name, &nameLength);
		if (!out)
			return ParsingGCodeResult::INVALID_ARGUMENT;

		auto it = programms.begin();
		for (; it != programms.end(); it++)
			if (strncmp(it->name, name, 20) == 0)
				break;

		if (it != programms.end())
			programms.erase(it);

		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "P90", 3)) {
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::CODE_FAILURE; // TODO: make sure this never arises

		char name[20];
		uint16_t nameLength = 0;
		bool out = getString(4, name, &nameLength);
		if (!out)
			return ParsingGCodeResult::INVALID_ARGUMENT;
		auto it = programms.begin();
		for (; it != programms.end(); it++)
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
		programmingMode.store(ProgrammingMode::PROGRAMMING);
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "P91", 3)) {
		if (commandDestination != activeProgram->header || programmingMode != ProgrammingMode::PROGRAMMING)
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		if (activeProgram->header->size() == 0)
			return ParsingGCodeResult::NON_CLOSED_LOOP;

		commandDestination = activeProgram->main;
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "P29", 3)) {
		if (commandDestination != activeProgram->header || programmingMode != ProgrammingMode::PROGRAMMING) // we can only declare looped command in the header
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		activeProgram->repeatIndefinitely = true;
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "P92", 3)) {
		if (programmingMode != ProgrammingMode::PROGRAMMING)
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		if (activeProgram->forLoopCounter != 0) {
			return ParsingGCodeResult::NON_CLOSED_LOOP;
		} else {
			activeProgram->reset();
			activeProgram = nullptr;
		}

		programmingMode.store(ProgrammingMode::NO_PROGRAMM);

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
		commandDestination->push_back(command);
		return ParsingGCodeResult::SUCCESS;
	}

endInvalidCommand:
	deleteCommand();
	return ParsingGCodeResult::INVALID_COMMAND;

endFailedToLockQueue:
	deleteCommand();
	return ParsingGCodeResult::FAILED_TO_LOCK_QUEUE;

endInvalidArgument:
	deleteCommand();
	return ParsingGCodeResult::INVALID_ARGUMENT;

endBadContext:
	deleteCommand();
	return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
}

void StepperControl::endstopHHandler(void* arg)
{
	if (stepperControl.programmingMode == ProgrammingMode::HOMING) {
		steppers.stopStepperH();
		xEventGroupSetBits(StepperControl::homingEventGroup, STEPPER_COMPLETE_BIT_H);
	}
}

void StepperControl::endstopTHandler(void* arg)
{
	if (stepperControl.programmingMode == ProgrammingMode::HOMING) {
		steppers.stopStepperT();
		xEventGroupSetBits(StepperControl::homingEventGroup, STEPPER_COMPLETE_BIT_T);
	}
}

void StepperControl::home()
{
	ESP_LOGI(TAG, "Home | Starting homing routine");
	// stop the steppers
	steppers.stopNowStepperH();
	steppers.stopNowStepperT();
	// set the positioning mode to homing
	programmingMode.store(ProgrammingMode::HOMING);

	// attach interrupts
	attachInterruptArg(CONFIG_STEPPER_H_PIN_ENDSTOP, StepperControl::endstopTHandler, NULL, CHANGE);
	attachInterruptArg(CONFIG_STEPPER_T_PIN_ENDSTOP, StepperControl::endstopHHandler, NULL, CHANGE);

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

	// varsH.stepNumber.store(0); TODO
	// varsT.stepNumber.store(0);

	// cleanup
	xEventGroupClearBits(homingEventGroup, HOMING_DONE_BIT);
	detachInterrupt(CONFIG_STEPPER_H_PIN_ENDSTOP);
	detachInterrupt(CONFIG_STEPPER_T_PIN_ENDSTOP);
}
