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

void StepperControl::init()
{

	steppers.initMCPWN();
	steppers.initPCNT();
	steppers.initTimers();

	steppers.initStepperTasks();
	StepperControl::homingEventGroup = xEventGroupCreate();

	pinMode(CONFIG_STEPPER_H_PIN_EN, OUTPUT);
	pinMode(CONFIG_STEPPER_T_PIN_EN, OUTPUT);
	ESP_LOGI(TAG, "commandSchedulerTask | stepper H\n\tenable pin %d\n\tendstop pin %d\n\tstep pin %d\n\tsense pin %d\n\tdirection pin %d", CONFIG_STEPPER_H_PIN_EN, CONFIG_STEPPER_H_PIN_ENDSTOP, CONFIG_STEPPER_H_PIN_STEP, CONFIG_STEPPER_H_PIN_SENSE, CONFIG_STEPPER_H_PIN_DIR);
	ESP_LOGI(TAG, "commandSchedulerTask | stepper T\n\tenable pin %d\n\tendstop pin %d\n\tstep pin %d\n\tsense pin %d\n\tdirection pin %d", CONFIG_STEPPER_T_PIN_EN, CONFIG_STEPPER_T_PIN_ENDSTOP, CONFIG_STEPPER_T_PIN_STEP, CONFIG_STEPPER_T_PIN_SENSE, CONFIG_STEPPER_T_PIN_DIR);
	gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_EN, 0);

	gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN_EN, 0);

	pinMode(CONFIG_STEPPER_H_PIN_ENDSTOP, INPUT);
	pinMode(CONFIG_STEPPER_T_PIN_ENDSTOP, INPUT);

	noProgrammQueueLock = xSemaphoreCreateMutex();

	if (noProgrammQueueLock == NULL) {
		ESP_LOGE(TAG, "JoPkaEndpoint | xSemaphoreCreateMutex failed");
	} else {
		ESP_LOGI(TAG, "JoPkaEndpoint | created lock");
	}
	// on first step there are some initializations taking place that make it much longer than the rest
	steppers.stepStepper(stepperHalT, 1, 1, true);
	steppers.stepStepper(stepperHalH, 1, 1, true);
	steppers.stepStepper(stepperHalT, -1, 1, true);
	steppers.stepStepper(stepperHalH, -1, 1, true);

	xTaskCreate(StepperControl::commandSchedulerTask, "commandSchedulerTask", 4096, NULL, 5, &commandSchedulerTaskHandle);
}

float StepperControl::getElementFloat(const char* str, const uint16_t length, const uint16_t startIndex, const char* matchString, const uint16_t elementLength)
{
	bool negative = false;
	float toReturn = 0;
	uint8_t decimal = 0;
	for (uint16_t i = startIndex; i < length; i++) {
		if (str[i] == matchString[0]) {
			if (strncmp(str + i, matchString, elementLength) == 0) {

				// fix cases where matchString="T" will match "ST" and fail
				if (str[i - 1] >= 'A' && str[i - 1] <= 'Z')
					continue;

				i += elementLength;
				// fix cases where matchString="S" will match "ST" and fail
				if (!(str[i] == ' ' || str[i] == '-' || (str[i] >= '0' && str[i] <= '9')))
					continue;

				while (str[i] == ' ' && i < length)
					i++;

				if (str[i] == '-') {
					negative = true;
					i++;
				}
				while (str[i] != ' ' && i < length) {
					if (str[i] >= '0' && str[i] <= '9') {
						toReturn = toReturn * 10 + (str[i] - '0');
						if (decimal) {
							decimal++;
						}
						i++;
						continue;
					} else if (str[i] == '.' && !decimal) {
						decimal = 1;
						i++;
						continue;
					}
					return NAN;
				}
				toReturn /= pow(10, decimal);
				return negative ? -toReturn : toReturn;
			}
		}
	}
	return NAN;
}

int32_t StepperControl::getElementInt(const char* str, const uint16_t length, const uint16_t startIndex, const char* matchString, const uint16_t elementLength)
{
	bool negative = false;
	int32_t toReturn = 0;
	uint16_t iMatch = 0;
	for (uint16_t i = startIndex; i < length; i++) {
		if (str[i] == matchString[0]) {
			if (strncmp(str + i, matchString, elementLength) == 0) {

				// fix cases where matchString="T" will match "ST" and fail
				if (str[i - 1] >= 'A' && str[i - 1] <= 'Z')
					continue;

				i += elementLength;
				// fix cases where matchString="S" will match "ST" and fail
				if (!(str[i] == ' ' || str[i] == '-' || (str[i] >= '0' && str[i] <= '9')))
					continue;

				while (str[i] == ' ' && i < length)
					i++;

				if (str[i] == '-') {
					negative = true;
					i++;
				}
				while (str[i] != ' ' && i < length) {
					if (str[i] >= '0' && str[i] <= '9') {
						toReturn = toReturn * 10 + (str[i] - '0');
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
}

bool StepperControl::getElementString(const char* str, const uint16_t length, const uint16_t startIndex, const char* matchString, const uint16_t elementLength)
{
	for (uint16_t i = startIndex; i < length; i++) {
		if (str[i] == matchString[0]) {
			if (strncmp(str + i, matchString, elementLength) == 0) {
				return true;
			}
		}
	}
	return false;
}

int32_t StepperControl::moveStepperAbsolute(stepper_hal_struct_t* stepperHal, gcode_command_movement_t* movement, const stepper_operation_paramters_t* stepperOpPar, bool synchronized)
{
	if (stepperOpPar->stepsMax == GCODE_ELEMENT_INVALID_INT && stepperOpPar->stepsMin == GCODE_ELEMENT_INVALID_INT) {
#ifdef CONFIG_APP_DEBUG
		ESP_LOGI(TAG, "moveStepperAbsolute | %s no limits", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T");
#endif
		steppers.stepStepper(stepperHalH, ANGLE_DISTANCE(stepperOpPar->positionLastScheduled, movement->val.steps, stepperHal->stepCount), movement->rpm, synchronized);
	} else if (stepperOpPar->stepsMax >= stepperOpPar->stepsMin) { // moving in interval <min, max>
#ifdef CONFIG_APP_DEBUG
		ESP_LOGI(TAG, "moveStepperAbsolute | %s in interval <min, max>", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T");
#endif
		if (movement->val.steps > stepperOpPar->stepsMax)
			movement->val.steps = stepperOpPar->stepsMax;
		else if (movement->val.steps < stepperOpPar->stepsMin)
			movement->val.steps = stepperOpPar->stepsMin;

		// now we need to pick from clockwise or counterclockwise rotation, where one can possible go outside of the limits
		if (stepperOpPar->positionLastScheduled < movement->val.steps)
			steppers.stepStepper(stepperHal, ANGLE_DISTANCE_CLOCKWISE(stepperOpPar->positionLastScheduled, movement->val.steps, stepperHal->stepCount), movement->rpm, synchronized);
		else
			steppers.stepStepper(stepperHal, ANGLE_DISTANCE_COUNTERCLOCKWISE(stepperOpPar->positionLastScheduled, movement->val.steps, stepperHal->stepCount), movement->rpm, synchronized);
	} else { // moving in interval <0, max> U <min, stepCount>
#ifdef CONFIG_APP_DEBUG
		ESP_LOGI(TAG, "moveStepperAbsolute | %s in interval <0, max> U <min, stepCount>", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "H" : "T");
#endif
		uint32_t avg = (stepperOpPar->stepsMax + stepperOpPar->stepsMin) / 2;
		if (movement->val.steps >= avg && movement->val.steps < stepperOpPar->stepsMin)
			movement->val.steps = stepperOpPar->stepsMin;
		else if (movement->val.steps < avg && movement->val.steps > stepperOpPar->stepsMax)
			movement->val.steps = stepperOpPar->stepsMax;

		bool fromInInterval1 = (stepperOpPar->positionLastScheduled >= 0 && stepperOpPar->positionLastScheduled <= stepperOpPar->stepsMax);
		bool fromInInterval2 = (stepperOpPar->positionLastScheduled >= stepperOpPar->stepsMin && stepperOpPar->positionLastScheduled <= stepperHal->stepCount);
		bool destInInterval1 = (movement->val.steps >= 0 && movement->val.steps <= stepperOpPar->stepsMax);
		bool destInInterval2 = (movement->val.steps >= stepperOpPar->stepsMin && movement->val.steps <= stepperHal->stepCount);

		bool order = false;

		if (fromInInterval1 && destInInterval1)
			order = stepperOpPar->positionLastScheduled < movement->val.steps;
		else if (fromInInterval2 && destInInterval2)
			order = stepperOpPar->positionLastScheduled < movement->val.steps;
		else if (fromInInterval1 && destInInterval2)
			order = true;
		else if (fromInInterval2 && destInInterval1)
			order = true;

		// we need to calculete in which order steps and positionLastScheduled are
		if (order)
			steppers.stepStepper(stepperHal, ANGLE_DISTANCE_CLOCKWISE(stepperOpPar->positionLastScheduled, movement->val.steps, stepperHal->stepCount), movement->rpm, synchronized);
		else
			steppers.stepStepper(stepperHal, ANGLE_DISTANCE_CLOCKWISE(stepperOpPar->positionLastScheduled, movement->val.steps, stepperHal->stepCount), movement->rpm, synchronized);
	}
	return movement->val.steps;
}

int32_t StepperControl::moveStepperRelative(stepper_hal_struct_t* stepperHal, gcode_command_movement_t* movement, const stepper_operation_paramters_t* stepperOpPar, bool synchronized)
{
	if (stepperOpPar->stepsMax == GCODE_ELEMENT_INVALID_INT && stepperOpPar->stepsMin == GCODE_ELEMENT_INVALID_INT) {
		steppers.stepStepper(stepperHal, movement->val.steps, movement->rpm, synchronized);
	} else if (stepperOpPar->stepsMax >= stepperOpPar->stepsMin) { // moving in interval <min, max>
		if (stepperOpPar->positionLastScheduled + movement->val.steps >= stepperOpPar->stepsMax)
			movement->val.steps = stepperOpPar->stepsMax - stepperOpPar->positionLastScheduled;
		else if (stepperOpPar->positionLastScheduled + movement->val.steps <= stepperOpPar->stepsMin)
			movement->val.steps = stepperOpPar->stepsMin - stepperOpPar->positionLastScheduled;

		steppers.stepStepper(stepperHal, movement->val.steps, movement->rpm, synchronized);
	} else { // moving in interval <0, max> U <min, stepCount>

		int16_t maxStepsCCW = (stepperOpPar->positionLastScheduled >= stepperOpPar->stepsMin) ? -(stepperOpPar->positionLastScheduled - stepperOpPar->stepsMin) : -(stepperOpPar->positionLastScheduled + stepperHal->stepCount - stepperOpPar->stepsMin);
		// Calculate stepperOpPar->stepsMaximum allowable steps clockwise (positive)
		int16_t maxStepsCW = (stepperOpPar->positionLastScheduled <= stepperOpPar->stepsMax) ? (stepperOpPar->stepsMax - stepperOpPar->positionLastScheduled) : (stepperHal->stepCount - stepperOpPar->positionLastScheduled + stepperOpPar->stepsMax);
		if (movement->val.steps < maxStepsCCW)
			movement->val.steps = maxStepsCCW;
		else if (movement->val.steps > maxStepsCW)
			movement->val.steps = maxStepsCW;
		steppers.stepStepper(stepperHal, movement->val.steps, movement->rpm, synchronized);
	}
	return NORMALIZE_ANGLE(stepperOpPar->positionLastScheduled + movement->val.steps, stepperHal->stepCount);
}

void StepperControl::commandSchedulerTask(void* arg)
{
	stepper_operation_paramters_t stepperOpParH;
	stepper_operation_paramters_t stepperOpParT;
	Unit unit = Unit::STEPS; // only used in motorTask

	gcode_command_t* command = nullptr; // NOTE: this is just hack so I don't have tu turn off -Werror=maybe-uninitialized

	// checks order of from and dest when in union of intervals <0, min> U <max, stepCount>
	// uint8_t  tmp = 0;
	// true if from comes before dest -> we need to move clockwise

	while (true) {
		stepperOpParH.position += steppers.getStepsTraveledOfPrevCommand(stepperHalH);
		stepperOpParT.position += steppers.getStepsTraveledOfPrevCommand(stepperHalT);


		// tmp++;
		// if(tmp == 20){
		// 	tmp =0;
		// 	int64_t travelledH = steppers.getStepsTraveledOfCurrentCommand(stepperHalH);
		// 	int64_t travelledT = steppers.getStepsTraveledOfCurrentCommand(stepperHalT);
		// 	ESP_LOGI(TAG, "H position %lld, H traveled %lld, T position %lld, T traveled %lld", stepperOpParH.position, travelledH, stepperOpParT.position, travelledT);
		// 	ESP_LOGI(TAG, "!P %lld, %f, %f\n", esp_timer_get_time()/1000, STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParH.position + travelledH, CONFIG_STEPPER_H_STEP_COUNT), CONFIG_STEPPER_H_STEP_COUNT), STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParT.position + travelledT, CONFIG_STEPPER_H_STEP_COUNT), CONFIG_STEPPER_H_STEP_COUNT));
		// }
		printf("!P %lld, %f, %f\n", esp_timer_get_time()/1000, STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParH.position + steppers.getStepsTraveledOfCurrentCommand(stepperHalH), CONFIG_STEPPER_H_STEP_COUNT), CONFIG_STEPPER_H_STEP_COUNT), STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParT.position + steppers.getStepsTraveledOfCurrentCommand(stepperHalT), CONFIG_STEPPER_T_STEP_COUNT), CONFIG_STEPPER_T_STEP_COUNT));
		// if queues are filled we will wait

		if (steppers.getQueueLength(stepperHalH) == CONFIG_STEPPER_HAL_QUEUE_SIZE || steppers.getQueueLength(stepperHalT) == CONFIG_STEPPER_HAL_QUEUE_SIZE) {
			vTaskDelay(20 / portTICK_PERIOD_MS);
			continue;
		}

		command = nullptr;

		if (programmingMode == ProgrammingMode::NO_PROGRAMM) {
			if (xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000) == pdTRUE) {
				if (noProgrammQueue.size() > 0) {
#ifdef CONFIG_APP_DEBUG
					// ESP_LOGI(TAG, "commandSchedulerTask | CMD SOURCE: no programm queue");
#endif
					command = noProgrammQueue.front();
					noProgrammQueue.pop();
				}
				xSemaphoreGive(noProgrammQueueLock);
			}
		} else if (programmingMode == ProgrammingMode::RUN_PROGRAM) {
			if (activeProgram->indexHeader != activeProgram->header->size()) { // running header
#ifdef CONFIG_APP_DEBUG
				// ESP_LOGI(TAG, "commandSchedulerTask | CMD SOURCE: programm header");
#endif
				command = activeProgram->header->at(activeProgram->indexHeader);
				activeProgram->indexHeader++;
			} else if (activeProgram->indexMain == 0 && activeProgram->indexMain != activeProgram->main->size()) { // header finished, switch to main
#ifdef CONFIG_APP_DEBUG
				// ESP_LOGI(TAG, "commandSchedulerTask | CMD SOURCE: programm main (start)");
#endif
				command = activeProgram->main->at(0);
				activeProgram->indexMain = 1;
			} else if (activeProgram->indexMain != activeProgram->main->size()) { // running main
#ifdef CONFIG_APP_DEBUG
				// ESP_LOGI(TAG, "commandSchedulerTask | CMD SOURCE: programm main");
#endif
				command = activeProgram->main->at(activeProgram->indexMain);
				activeProgram->indexMain++;
			} else if (activeProgram->repeatIndefinitely) { // finished main but repeat indefinitely is on
#ifdef CONFIG_APP_DEBUG
				// ESP_LOGI(TAG, "commandSchedulerTask | CMD SOURCE: programm main (repeat)");
#endif
				if(activeProgram->main->size() == 0){
					vTaskDelay(20 / portTICK_PERIOD_MS);
					continue;
				}
				command = activeProgram->main->at(0);
				activeProgram->indexMain = 1;
			} else { // finished main
#ifdef CONFIG_APP_DEBUG
				// ESP_LOGI(TAG, "commandSchedulerTask | CMD SOURCE: programm main (end)");
#endif
				steppers.stopStepper(stepperHalH);
				steppers.stopStepper(stepperHalT);
				activeProgram = nullptr;
				programmingMode.store(ProgrammingMode::NO_PROGRAMM);
			}
		}

		if (command == nullptr) {
			vTaskDelay(20 / portTICK_PERIOD_MS); // there are no commands to process, we can wait and will only refresh the position
			continue;
		}

		// reporting will be always is always in absolute position
		switch (command->type) {
		case GCodeCommand::G20:
#ifdef CONFIG_APP_DEBUG
			ESP_LOGI(TAG, "commandSchedulerTask | G20");
#endif
			unit = Unit::DEGREES;
			break;
		case GCodeCommand::G21:
#ifdef CONFIG_APP_DEBUG
			ESP_LOGI(TAG, "commandSchedulerTask | G21");
#endif
			unit = Unit::STEPS;
			break;
		case GCodeCommand::G90:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G90 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G90 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G90 | T");
#endif
			if (command->movementH != nullptr)
				stepperOpParH.positioningMode = PositioningMode::ABSOLUTE;
			if (command->movementT != nullptr)
				stepperOpParT.positioningMode = PositioningMode::ABSOLUTE;
			break;
		case GCodeCommand::G91:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G91 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G91 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G91 | T");
#endif
			if (command->movementH != nullptr)
				stepperOpParH.positioningMode = PositioningMode::RELATIVE;
			if (command->movementT != nullptr)
				stepperOpParT.positioningMode = PositioningMode::RELATIVE;
			break;
		case GCodeCommand::G92:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G92 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G92 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G92 | T");
#endif
			if (command->movementH != nullptr) {
				if (steppers.getQueueLength(stepperHalH) != 0) {
					ESP_LOGE(TAG, "commandSchedulerTask | G92 | ERR: cannot reset position while there are commands in queue");
				} else {
					steppers.getStepsTraveledOfPrevCommand(stepperHalH); // clear previous command steps
					stepperOpParH.position = 0;
					stepperOpParH.positionLastScheduled = 0;
				}
			}
			if (command->movementT != nullptr) {
				if (steppers.getQueueLength(stepperHalT) != 0) {
					ESP_LOGE(TAG, "commandSchedulerTask | G92 | ERR: cannot reset position while there are commands in queue");
				} else {
					steppers.getStepsTraveledOfPrevCommand(stepperHalT); // clear previous command steps
					stepperOpParT.position = 0;
					stepperOpParT.positionLastScheduled = 0;
				}
			}

			break;
		case GCodeCommand::G28: {
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G28 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G28 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G28 | T");
#endif
			ProgrammingMode mode = programmingMode.load();
			programmingMode.store(ProgrammingMode::HOMING);
			if (command->movementH != nullptr) {
				stepperControl.homeH();
			}
			if (command->movementT != nullptr) {
				stepperControl.homeT();
			}
			programmingMode.store(mode);
		 break;
														}
		case GCodeCommand::G0:
			if (command->movementH != nullptr) {
				if (command->movementH->val.steps == 0)
					steppers.skipStepper(stepperHalH, SYNCHRONIZED);
				command->movementH->val.steps = unit == Unit::DEGREES ? ANGLE_TO_STEPS(command->movementH->val.steps, CONFIG_STEPPER_H_STEP_COUNT) : command->movementH->val.steps;
				if (stepperOpParH.positioningMode == PositioningMode::ABSOLUTE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | H absolute");
#endif
					command->movementH->val.steps = NORMALIZE_ANGLE(command->movementH->val.steps, CONFIG_STEPPER_H_STEP_COUNT);
					stepperOpParH.positionLastScheduled = moveStepperAbsolute(stepperHalH, command->movementH, &stepperOpParH, SYNCHRONIZED);
				} else {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | H relative");
#endif
					stepperOpParH.positionLastScheduled = moveStepperRelative(stepperHalH, command->movementH, &stepperOpParH, SYNCHRONIZED);
				}
			}
			if (command->movementT != nullptr) {
				if (command->movementT->val.steps == 0)
					steppers.skipStepper(stepperHalT, SYNCHRONIZED);
				command->movementT->val.steps = unit == Unit::DEGREES ? ANGLE_TO_STEPS(command->movementT->val.steps, CONFIG_STEPPER_T_STEP_COUNT) : command->movementT->val.steps;
				if (stepperOpParT.positioningMode == PositioningMode::ABSOLUTE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | T absolute");
#endif
					command->movementT->val.steps = NORMALIZE_ANGLE(command->movementT->val.steps, CONFIG_STEPPER_T_STEP_COUNT);
					stepperOpParT.positionLastScheduled = moveStepperAbsolute(stepperHalT, command->movementT, &stepperOpParT, SYNCHRONIZED);
				} else {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | T relative");
#endif
					stepperOpParT.positionLastScheduled = moveStepperRelative(stepperHalT, command->movementT, &stepperOpParT, SYNCHRONIZED);
				}
			}
			break;
		case GCodeCommand::M03:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M03 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M03 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M03 | T");
#endif
			if (command->movementH != nullptr) {
				if (stepperOpParH.positioningMode != PositioningMode::RELATIVE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGE(TAG, "commandSchedulerTask | M03 | H ERR: command with absolute positioning is not supported, will switch to relative");
#endif
					stepperOpParH.positioningMode = PositioningMode::RELATIVE;
				}
				steppers.spindleStepper(stepperHalH, command->movementH->rpm, command->movementH->val.direction);
			}
			if (command->movementT != nullptr) {
				if (stepperOpParT.positioningMode != PositioningMode::RELATIVE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGE(TAG, "commandSchedulerTask | M03 | T ERR: command with absolute positioning is not supported, will switch to relative");
#endif
					stepperOpParT.positioningMode = PositioningMode::RELATIVE;
				}
				steppers.spindleStepper(stepperHalT, command->movementT->rpm, command->movementT->val.direction);
			}
			break;
		case GCodeCommand::M05:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M05 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M05 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M05 | T");
#endif
			if (command->movementH != nullptr)
				steppers.stopStepper(stepperHalH, SYNCHRONIZED);
			if (command->movementT != nullptr)
				steppers.stopStepper(stepperHalT, SYNCHRONIZED);
			break;
		case GCodeCommand::M201:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M201 | H: min %ld, max %ld | T: min %ld, max %ld", stepperOpParH.stepsMin, stepperOpParH.stepsMax, stepperOpParT.stepsMin, stepperOpParT.stepsMax);
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M201 | H: min %ld, max %ld", stepperOpParH.stepsMin, stepperOpParH.stepsMax);
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M201 | T: min %ld, max %ld", stepperOpParT.stepsMin, stepperOpParT.stepsMax);
#endif
			if (unit == Unit::STEPS) {
				if (command->movementH != nullptr) {
					stepperOpParH.stepsMin = command->movementH->val.limits.min <= CONFIG_STEPPER_H_STEP_COUNT ? (uint32_t)command->movementH->val.limits.min : CONFIG_STEPPER_H_STEP_COUNT;
					stepperOpParH.stepsMax = command->movementH->val.limits.max <= CONFIG_STEPPER_H_STEP_COUNT ? (uint32_t)command->movementH->val.limits.max : CONFIG_STEPPER_H_STEP_COUNT;
				}
				if (command->movementT != nullptr) {
					stepperOpParT.stepsMin = command->movementT->val.limits.min <= CONFIG_STEPPER_T_STEP_COUNT ? (uint32_t)command->movementT->val.limits.min : CONFIG_STEPPER_T_STEP_COUNT;
					stepperOpParT.stepsMax = command->movementT->val.limits.max <= CONFIG_STEPPER_T_STEP_COUNT ? (uint32_t)command->movementT->val.limits.max : CONFIG_STEPPER_T_STEP_COUNT;
				}
			} else {
				if (command->movementH != nullptr) {
					stepperOpParH.stepsMin = ANGLE_TO_STEPS(command->movementH->val.limits.min, CONFIG_STEPPER_H_STEP_COUNT) <= CONFIG_STEPPER_H_STEP_COUNT ? ANGLE_TO_STEPS(command->movementH->val.limits.min, CONFIG_STEPPER_H_STEP_COUNT) : CONFIG_STEPPER_H_STEP_COUNT;
					stepperOpParH.stepsMax = ANGLE_TO_STEPS(command->movementH->val.limits.max, CONFIG_STEPPER_H_STEP_COUNT) <= CONFIG_STEPPER_H_STEP_COUNT ? ANGLE_TO_STEPS(command->movementH->val.limits.max, CONFIG_STEPPER_H_STEP_COUNT) : CONFIG_STEPPER_H_STEP_COUNT;
				}
				if (command->movementT != nullptr) {
					stepperOpParT.stepsMin = ANGLE_TO_STEPS(command->movementT->val.limits.min, CONFIG_STEPPER_H_STEP_COUNT) <= CONFIG_STEPPER_T_STEP_COUNT ? ANGLE_TO_STEPS(command->movementT->val.limits.min, CONFIG_STEPPER_H_STEP_COUNT) : CONFIG_STEPPER_T_STEP_COUNT;
					stepperOpParT.stepsMax = ANGLE_TO_STEPS(command->movementT->val.limits.max, CONFIG_STEPPER_H_STEP_COUNT) <= CONFIG_STEPPER_T_STEP_COUNT ? ANGLE_TO_STEPS(command->movementT->val.limits.max, CONFIG_STEPPER_H_STEP_COUNT) : CONFIG_STEPPER_T_STEP_COUNT;
				}
			}
			break;

		case GCodeCommand::M202:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M202 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M202 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M202 | T");
#endif
			if (command->movementH != nullptr) {
				stepperOpParH.stepsMin = GCODE_ELEMENT_INVALID_INT;
				stepperOpParH.stepsMax = GCODE_ELEMENT_INVALID_INT;
			}
			if (command->movementT != nullptr) {
				stepperOpParT.stepsMin = GCODE_ELEMENT_INVALID_INT;
				stepperOpParT.stepsMax = GCODE_ELEMENT_INVALID_INT;
			}
			break;
		case GCodeCommand::P21:
#ifdef CONFIG_APP_DEBUG
			ESP_LOGI(TAG, "commandSchedulerTask | P21 | %d", activeProgram->forLoopCounter);
#endif
			activeProgram->forLoopCounter = command->movementH->val.iterations;
			if (activeProgram->indexHeader != activeProgram->header->size())
				activeProgram->indexForLoop = activeProgram->indexHeader + 1;
			else
				activeProgram->indexForLoop = activeProgram->indexMain + 1;
			break;
		case GCodeCommand::P22:
#ifdef CONFIG_APP_DEBUG
			ESP_LOGI(TAG, "commandSchedulerTask | P22 | %d", activeProgram->forLoopCounter);
#endif
			if (activeProgram->forLoopCounter > 0) {
				activeProgram->forLoopCounter--;
				if (activeProgram->indexHeader != activeProgram->header->size())
					activeProgram->indexHeader = activeProgram->indexForLoop;
				else
					activeProgram->indexMain = activeProgram->indexForLoop;
			}
			break;
		case GCodeCommand::W1:
#ifdef CONFIG_APP_DEBUG
			if (command->movementH != nullptr && command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | H, T");
			else if (command->movementH != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | H");
			else if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | T");
#endif
			if (command->movementH != nullptr) {
				steppers.waitStepper(stepperHalH, command->movementH->val.time, SYNCHRONIZED);
			}
			if (command->movementT != nullptr) {
				steppers.waitStepper(stepperHalT, command->movementT->val.time, SYNCHRONIZED);
			}
			break;
		case GCodeCommand::W3:
#ifdef CONFIG_APP_DEBUG
			if (command->movementT != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | T");
#endif
			if (command->movementT != nullptr) {
				vTaskDelay(command->movementT->val.time / portTICK_PERIOD_MS);
			}
			break;
		default:
			break;
		}
		if (programmingMode == ProgrammingMode::NO_PROGRAMM)
			delete command;
	}
	vTaskDelete(NULL);
}

ParsingGCodeResult StepperControl::parseGCode(const char* gcode, const uint16_t length)
{
// #ifdef CONFIG_COMM_DEBUG
// 	char* gcodeCopy = (char*)malloc(length + 1);
// 	strncpy(gcodeCopy, gcode, length);
// 	gcodeCopy[length] = '\0';
// 	ESP_LOGI(TAG, "Received command: %s", gcodeCopy);
// 	free(gcodeCopy);
// #endif /* CONFIG_COMM_DEBUG */

	ParsingGCodeResult res = parseGCodeNonScheduledCommands(gcode, length);
	if (res != ParsingGCodeResult::RESERVED)
		return res;

	if (programmingMode == ProgrammingMode::RUN_PROGRAM) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode | Device is in RUN_PROGRAM mode");
#endif /* CONFIG_COMM_DEBUG */
		return ParsingGCodeResult::NOT_PROCESSING_COMMANDS;
	}

	gcode_command_t* command = new gcode_command_t();

	switch (gcode[0]) {
	case 'G':
		res = parseGCodeGCommands(gcode, length, command);
		break;
	case 'M':
		res = parseGCodeMCommands(gcode, length, command);
		break;
	case 'W':
		res = parseGCodeWCommands(gcode, length, command);
		break;
	case 'P':
		res = parseGCodePCommands(gcode, length, command);
		break;
	default:
		res = ParsingGCodeResult::INVALID_COMMAND;
		break;
	}

	if (res == ParsingGCodeResult::SUCCESS) {
		if (command->type == GCodeCommand::COMMAND_TO_REMOVE) {
			delete command;
			return ParsingGCodeResult::SUCCESS;
		}
		if (programmingMode == ProgrammingMode::NO_PROGRAMM) {
			if (xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000) == pdTRUE) {
				noProgrammQueue.push(command);
				xSemaphoreGive(noProgrammQueueLock);
				return ParsingGCodeResult::SUCCESS;
			} else {
				delete command;
				return ParsingGCodeResult::FAILED_TO_LOCK_QUEUE;
			}
		} else {
			commandDestination->push_back(command);
			return ParsingGCodeResult::SUCCESS;
		}
	} else {
		delete command;
		return res;
	}
}

ParsingGCodeResult StepperControl::parseGCodeNonScheduledCommands(const char* gcode, const uint16_t length)
{
	int64_t elementInt = 0;
	float elementFloat = 0;

	if (strncmp(gcode, "M80", 3) == 0) { // power down high voltage supply
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode | M80");
#endif /* CONFIG_COMM_DEBUG */
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_EN, 0);
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN_EN, 0);

		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "M81", 3) == 0) { // power up high voltage supply
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode | M81");
#endif /* CONFIG_COMM_DEBUG */
		activeProgram = nullptr;
		programmingMode.store(ProgrammingMode::NO_PROGRAMM);
		xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000);
		while (!noProgrammQueue.empty()) {
			gcode_command_t* c = noProgrammQueue.front();
			noProgrammQueue.pop();
			delete c;
		}
		noProgrammQueue = {};
		xSemaphoreGive(noProgrammQueueLock);
		steppers.stopNowStepper(stepperHalH);
		steppers.stopNowStepper(stepperHalT);
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_H_PIN_EN, 1);
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_T_PIN_EN, 1);
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "M82", 3) == 0) { // carefull stop
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode | M82");
#endif /* CONFIG_COMM_DEBUG */
		activeProgram = nullptr;
		programmingMode.store(ProgrammingMode::NO_PROGRAMM);
		xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000);
		while (!noProgrammQueue.empty()) {
			gcode_command_t* c = noProgrammQueue.front();
			noProgrammQueue.pop();
			delete c;
		}
		noProgrammQueue = {};
		xSemaphoreGive(noProgrammQueueLock);
		steppers.stopNowStepper(stepperHalH);
		steppers.stopNowStepper(stepperHalT);
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "G3", 2) == 0) { // override
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G3");
#endif /* CONFIG_COMM_DEBUG */
		gcode_command_movement_t* movementH = nullptr;
		gcode_command_movement_t* movementT = nullptr;
		elementInt = getElementInt(gcode, length, 3, "H", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			movementH = new gcode_command_movement_t();
			movementH->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementInt = getElementInt(gcode, length, 3, "T", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			movementT = new gcode_command_movement_t();
			movementT->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementFloat = getElementFloat(gcode, length, 3, "S", 1);
		if (!std::isnan(elementFloat)) {
			if (movementH != nullptr)
				movementH->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			if (movementT != nullptr) {
				movementT->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			}
		}

		elementFloat = getElementFloat(gcode, length, 3, "SH", 2);
		if (!std::isnan(elementFloat) && movementH != nullptr)
			movementH->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);

		elementFloat = getElementFloat(gcode, length, 3, "ST", 2);
		if (!std::isnan(elementFloat) && movementT != nullptr) {
			movementT->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
		}

		if ((movementH != nullptr && movementH->rpm == NAN && movementH->val.steps != 0) || (movementT != nullptr && movementT->rpm == NAN && movementT->val.steps != 0))
			return ParsingGCodeResult::INVALID_ARGUMENT;

		if(movementH == nullptr && movementT == nullptr)
			return ParsingGCodeResult::INVALID_ARGUMENT;

		if (movementH != nullptr && movementT != nullptr) {
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| G3 | H: %d, T: %d, SH: %f, ST %f", movementH->val.steps, movementT->val.steps, movementH->rpm, movementT->rpm);
#endif /* CONFIG_COMM_DEBUG */
			steppers.stepStepper(stepperHalH, movementH->val.steps, movementH->rpm, true);
			steppers.stepStepper(stepperHalH, movementT->val.steps, movementT->rpm, true);
			return ParsingGCodeResult::SUCCESS;
		}
		if (movementH != nullptr) {
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| G3 | H: %d, SH: %f", movementH->val.steps, movementH->rpm);
#endif /* CONFIG_COMM_DEBUG */
			steppers.stepStepper(stepperHalH, movementH->val.steps, movementH->rpm, false);
		}
		if (movementT != nullptr) {
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| G3 | T: %d, ST: %f", movementT->val.steps, movementT->rpm);
#endif /* CONFIG_COMM_DEBUG */
			steppers.stepStepper(stepperHalT, movementT->val.steps, movementT->rpm, false);
		}
		return ParsingGCodeResult::SUCCESS;

	} else if (strncmp(gcode, "P0", 2) == 0) { // stop programm execution
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P0");
#endif /* CONFIG_COMM_DEBUG */
		if (programmingMode != ProgrammingMode::RUN_PROGRAM)
			return ParsingGCodeResult::INVALID_COMMAND;

		if (activeProgram != nullptr) { // we are running a program so reset it and move back to NO_PROGRAMM mode
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| P0 | Stopping program");
#endif /* CONFIG_COMM_DEBUG */
			activeProgram->reset();
			activeProgram = nullptr;
		}
		steppers.stopStepper(stepperHalH);
		steppers.stopStepper(stepperHalT);
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P0 | Device is in NO_PROGRAMM mode");
#endif /* CONFIG_COMM_DEBUG */
		programmingMode.store(ProgrammingMode::NO_PROGRAMM);
		return ParsingGCodeResult::SUCCESS;
	}

	return ParsingGCodeResult::RESERVED;
}

ParsingGCodeResult StepperControl::parseGCodeGCommands(const char* gcode, const uint16_t length, gcode_command_t* command)
{
	int32_t elementInt = 0;
	float elementFloat = 0;

	if (strncmp(gcode, "G20", 3) == 0) { // set unit to degrees
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G20");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G20;
	} else if (strncmp(gcode, "G21", 3) == 0) { // set unit to steps
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G21");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G21;
	} else if (strncmp(gcode, "G90", 3) == 0) { // set the absolute positioning
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G90");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G90;
		if (!getElementString(gcode, length, 3, "H", 1) && !getElementString(gcode, length, 3, "T", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementT = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "H", 1)) {
			command->movementH = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "T", 1)) {
			command->movementT = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G91", 3) == 0) { // set the relative positioning
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G91");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G91;
		if (!getElementString(gcode, length, 3, "H", 1) && !getElementString(gcode, length, 3, "T", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementT = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "H", 1)) {
			command->movementH = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "T", 1)) {
			command->movementT = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G92", 3) == 0) { // set current position as home
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G92");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G92;
		if (!getElementString(gcode, length, 3, "H", 1) && !getElementString(gcode, length, 3, "T", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementT = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "H", 1)) {
			command->movementH = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "T", 1)) {
			command->movementT = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G28", 3) == 0) { // home both drivers
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G28");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G28;
		if (!getElementString(gcode, length, 3, "H", 1) && !getElementString(gcode, length, 3, "T", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementT = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "H", 1)) {
			command->movementH = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "T", 1)) {
			command->movementT = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G0", 2) == 0) { // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G0");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G0;
		elementInt = getElementInt(gcode, length, 3, "H", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementInt = getElementInt(gcode, length, 3, "T", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementFloat = getElementFloat(gcode, length, 3, "S", 1);
		if (!std::isnan(elementFloat)) {
			if (command->movementH != nullptr)
				command->movementH->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			if (command->movementT != nullptr) {
				command->movementT->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			}
		}

		elementFloat = getElementFloat(gcode, length, 3, "SH", 2);
		if (!std::isnan(elementFloat) && command->movementH != nullptr)
			command->movementH->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);

		elementFloat = getElementFloat(gcode, length, 3, "ST", 2);
		if (!std::isnan(elementFloat) && command->movementT != nullptr) {
			command->movementT->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
		}

		if ((command->movementH != nullptr && command->movementH->rpm == NAN && command->movementH->val.steps != 0) || (command->movementT != nullptr && command->movementT->rpm == NAN && command->movementT->val.steps != 0))
			return ParsingGCodeResult::INVALID_ARGUMENT;

		if(command->movementH == nullptr && command->movementT == nullptr)
			return ParsingGCodeResult::INVALID_ARGUMENT;

#ifdef CONFIG_COMM_DEBUG
		if (command->movementH != nullptr && command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| G0 | H: %d, T: %d, SH: %f, ST %f", command->movementH->val.steps, command->movementT->val.steps, command->movementH->rpm, command->movementT->rpm);
		else if (command->movementH != nullptr)
			ESP_LOGI(TAG, "parseGCode| G0 | H: %d, SH: %f", command->movementH->val.steps, command->movementH->rpm);
		else if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| G0 | T: %d, ST: %f", command->movementT->val.steps, command->movementT->rpm);
#endif /* CONFIG_COMM_DEBUG */

	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	return ParsingGCodeResult::SUCCESS;
}

ParsingGCodeResult StepperControl::parseGCodeMCommands(const char* gcode, const uint16_t length, gcode_command_t* command)
{
	int64_t elementInt = 0;
	float elementFloat = 0;

	if (strncmp(gcode, "M03", 3) == 0) { // start spinning horzMot axis clockwise
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M03");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::M03;

		if (getElementString(gcode, length, 3, "H+", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::FORWARD;
		} else if (getElementString(gcode, length, 3, "H-", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::BACKWARD;
		}

		if (getElementString(gcode, length, 3, "T+", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::FORWARD;
		} else if (getElementString(gcode, length, 3, "T-", 1)) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.direction = Direction::BACKWARD;
		}

		elementFloat = getElementFloat(gcode, length, 3, "S", 1);
		if (!std::isnan(elementFloat)) {
			if (command->movementH != nullptr)
				command->movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			if (command->movementT != nullptr)
				command->movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		}

		elementFloat = getElementFloat(gcode, length, 3, "SH", 2);
		if (!std::isnan(elementFloat) && command->movementH != nullptr) {
			command->movementH->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		}

		elementFloat = getElementFloat(gcode, length, 3, "ST", 2);
		if (!std::isnan(elementFloat) && command->movementT != nullptr) {
			command->movementT->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		}

		if ((command->movementH != nullptr && command->movementH->rpm == NAN) || (command->movementT != nullptr && command->movementT->rpm == NAN))
			return ParsingGCodeResult::INVALID_ARGUMENT;

#ifdef CONFIG_COMM_DEBUG
		if (command->movementH != nullptr && command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| M03 | H: %d, T: %d, SH: %f, ST %f", command->movementH->val.direction, command->movementT->val.direction, command->movementH->rpm, command->movementT->rpm);
		else if (command->movementH != nullptr)
			ESP_LOGI(TAG, "parseGCode| M03 | H: %d, SH: %f", command->movementH->val.direction, command->movementH->rpm);
		else if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| M03 | T: %d, ST: %f", command->movementT->val.direction, command->movementT->rpm);

#endif /* CONFIG_COMM_DEBUG */
	} else if (strncmp(gcode, "M05", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M05");
#endif /* CONFIG_COMM_DEBUG */

		command->type = GCodeCommand::M05;
		if (getElementString(gcode, length, 3, "H", 1)) {
			command->movementH = new gcode_command_movement_t(); // stepper will stop in command->movementT is not nullptr
		}

		if (getElementString(gcode, length, 3, "T", 1)) {
			command->movementT = new gcode_command_movement_t(); // stepper will stop in command->movementT is not nullptr
		}
	} else if (strncmp(gcode, "M201", 4) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M201");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::M201;
		if (getElementString(gcode, length, 5, "LH", 2) && getElementString(gcode, length, 5, "HH", 2)) {
			command->movementH = new gcode_command_movement_t();
			elementFloat = getElementFloat(gcode, length, 5, "LH", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementH->val.limits.min = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;

			elementFloat = getElementFloat(gcode, length, 5, "HH", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementH->val.limits.max = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;
		}

		if (getElementString(gcode, length, 5, "LT", 2) && getElementString(gcode, length, 5, "HT", 2)) {
			command->movementT = new gcode_command_movement_t();

			elementFloat = getElementFloat(gcode, length, 5, "LT", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementT->val.limits.min = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;

			elementFloat = getElementFloat(gcode, length, 5, "HT", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementT->val.limits.max = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementH != nullptr && command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| M201 | LH: %f, LT: %f, HH: %f, HT %f", command->movementH->val.limits.min, command->movementT->val.limits.min, command->movementH->val.limits.max, command->movementT->val.limits.max);
		else if (command->movementH != nullptr)
			ESP_LOGI(TAG, "parseGCode| M201 | H: %f, SH: %f", command->movementH->val.limits.min, command->movementH->val.limits.max);
		else if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| M201 | T: %f, ST: %f", command->movementT->val.limits.min, command->movementT->val.limits.max);
#endif /* CONFIG_COMM_DEBUG */

	} else if (strncmp(gcode, "M202", 4) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M202");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::M202;
		if (getElementString(gcode, length, 5, "H", 1)) {
			command->movementH = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 5, "T", 1)) {
			command->movementT = new gcode_command_movement_t();
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementH != nullptr && command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| M202 | H, T");
		else if (command->movementH != nullptr)
			ESP_LOGI(TAG, "parseGCode| M202 | H");
		else if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| M202 | T");
#endif /* CONFIG_COMM_DEBUG */
	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	return ParsingGCodeResult::SUCCESS;
}

ParsingGCodeResult StepperControl::parseGCodeWCommands(const char* gcode, const uint16_t length, gcode_command_t* command)
{
	int64_t elementInt = 0;
	float elementFloat = 0;

	if (strncmp(gcode, "W0", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| W0");
#endif																/* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::W1; // all W0 command futhers on will be handled as W1
		elementInt = getElementInt(gcode, length, 2, "H", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.time = elementInt * 1000;
		}

		elementInt = getElementInt(gcode, length, 2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.time = elementInt * 1000;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementH != nullptr && command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| W0 | H: %ld, T: %ld", command->movementH->val.time, command->movementT->val.time);
		else if (command->movementH != nullptr)
			ESP_LOGI(TAG, "parseGCode| W0 | H: %ld", command->movementH->val.time);
		else if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| W0 | T: %ld", command->movementT->val.time);
#endif /* CONFIG_COMM_DEBUG */
	} else if (strncmp(gcode, "W1", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| W1");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::W1;
		elementInt = getElementInt(gcode, length, 2, "H", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.time = elementInt;
		}

		elementInt = getElementInt(gcode, length, 2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.time = elementInt;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementH != nullptr && command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| W1 | H: %ld, T: %ld", command->movementH->val.time, command->movementT->val.time);
		else if (command->movementH != nullptr)
			ESP_LOGI(TAG, "parseGCode| W1 | H: %ld", command->movementH->val.time);
		else if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| W1 | T: %ld", command->movementT->val.time);
#endif /* CONFIG_COMM_DEBUG */;
	} else if (strncmp(gcode, "W3", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| W3");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::W3;
		elementInt = getElementInt(gcode, length, 2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementT = new gcode_command_movement_t();
			command->movementT->val.time = elementInt;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementT != nullptr)
			ESP_LOGI(TAG, "parseGCode| W3 | T: %ld", command->movementT->val.time);
#endif /* CONFIG_COMM_DEBUG */;
	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	return ParsingGCodeResult::SUCCESS;
}

ParsingGCodeResult StepperControl::parseGCodePCommands(const char* gcode, const uint16_t length, gcode_command_t* command)
{
	auto getString = [gcode, length](uint16_t index, char* str, uint16_t* stringLength) -> bool {
		for (uint16_t i = index; i < length; i++) {
			if (gcode[i] == ' ')
				break;

			str[*stringLength] = gcode[i];
			(*stringLength)++;
		}
		if (*stringLength == 0)
			return false;
		else
			return true;
	};
	int64_t elementInt = 0;
	float elementFloat = 0;

	if (strncmp(gcode, "P21", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P21");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::P21;
		if (programmingMode != ProgrammingMode::PROGRAMMING)
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		if (activeProgram->forLoopCounter != 0)
			return ParsingGCodeResult::NON_CLOSED_LOOP;

		activeProgram->forLoopCounter++;

		elementInt = getElementInt(gcode, length, 3, "I", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT || elementInt < 0) {
			command->movementH = new gcode_command_movement_t();
			command->movementH->val.iterations = elementInt;
		} else
			return ParsingGCodeResult::INVALID_ARGUMENT;
	} else if (strncmp(gcode, "P22", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P22");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::P22;
		if (programmingMode != ProgrammingMode::PROGRAMMING) {
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		}
		activeProgram->forLoopCounter--;
	} else if (strncmp(gcode, "P1", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P1");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::NOT_PROCESSING_COMMANDS; // TODO: make sure this never arises

		char name[PROG_NAME_MAX_LENGTH] = {};
		uint16_t nameLength = 0;
		bool out = getString(3, name, &nameLength);
		if (!out)
			return ParsingGCodeResult::INVALID_ARGUMENT;

		// switch programmingMode, switch activeProgram
		auto it = programms.begin();
		for (; it != programms.end(); it++)
			if (strncmp(it->name, name, PROG_NAME_MAX_LENGTH) == 0)
				break;

		if (it == programms.end())
			return ParsingGCodeResult::INVALID_ARGUMENT;

		activeProgram = &(*it);
		activeProgram->reset(); // we never do cleanup on program end
		programmingMode.store(ProgrammingMode::RUN_PROGRAM);
		xSemaphoreTake(noProgrammQueueLock, (TickType_t)1000);
		while (!noProgrammQueue.empty()) {
			gcode_command_t* c = noProgrammQueue.front();
			noProgrammQueue.pop();
			delete c;
		}
		noProgrammQueue = {};
		xSemaphoreGive(noProgrammQueueLock);
		steppers.stopNowStepper(stepperHalT);
		steppers.stopNowStepper(stepperHalH);
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P1 | Running program %s", activeProgram->name);
#endif /* CONFIG_COMM_DEBUG */

	} else if (strncmp(gcode, "P90", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P90");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr){
			ESP_LOGE(TAG, "parseGCode| P90 | programmingMode: %d, activeProgram: %p", programmingMode.load(), activeProgram);
			return ParsingGCodeResult::CODE_FAILURE; // TODO: make sure this never arises
		}

		char name[PROG_NAME_MAX_LENGTH] = {};
		uint16_t nameLength = 0;
		bool out = getString(4, name, &nameLength);
		if (!out)
			return ParsingGCodeResult::INVALID_ARGUMENT;
		auto it = programms.begin();
		for (; it != programms.end(); it++)
			if (strncmp(it->name, name, PROG_NAME_MAX_LENGTH) == 0)
				break;
		if (it != programms.end()) { // programm with the same name exists so we will overwrite it
			// -> if there is a program with the same name we will overwrite it
			activeProgram = &(*it);
			activeProgram->clean();
			commandDestination = activeProgram->header;
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| P90 | Overwriting program %s", name);
#endif			 /* CONFIG_COMM_DEBUG */
		} else { // create new programm
			gcode_programm_t newProgram;
			strncpy(newProgram.name, name, PROG_NAME_MAX_LENGTH);
			programms.push_back(newProgram);
			activeProgram = &(programms.back());
			commandDestination = activeProgram->header;
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| P90 | Creating program %s", name);
#endif /* CONFIG_COMM_DEBUG */
		}
		programmingMode.store(ProgrammingMode::PROGRAMMING);
	} else if (strncmp(gcode, "P91", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P91");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (commandDestination != activeProgram->header || programmingMode != ProgrammingMode::PROGRAMMING)
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		if (activeProgram->header->size() == 0)
			return ParsingGCodeResult::NON_CLOSED_LOOP;

		commandDestination = activeProgram->main;
	} else if (strncmp(gcode, "P29", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P29");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (commandDestination != activeProgram->header || programmingMode != ProgrammingMode::PROGRAMMING) // we can only declare looped command in the header
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		activeProgram->repeatIndefinitely = true;
	} else if (strncmp(gcode, "P2", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P2");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (programmingMode != ProgrammingMode::NO_PROGRAMM || activeProgram != nullptr)
			return ParsingGCodeResult::NOT_PROCESSING_COMMANDS; // TODO: make sure this never arises

		char name[PROG_NAME_MAX_LENGTH] = {};
		uint16_t nameLength = 0;
		bool out = getString(3, name, &nameLength);
		if (!out)
			return ParsingGCodeResult::INVALID_ARGUMENT;

		auto it = programms.begin();
		for (; it != programms.end(); it++) {
			ESP_LOGI(TAG, "parseGCode| P2 | %s", it->name);

			if (strncmp(it->name, name, PROG_NAME_MAX_LENGTH) == 0)
				break;
		}

		if (it != programms.end())
			programms.erase(it);

#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P2 | Deleting %s, Currently stored programs: ", name);
		for (auto& p : programms)
			ESP_LOGI(TAG, "parseGCode| P2 | %s", p.name);
#endif /* CONFIG_COMM_DEBUG */

	} else if (strncmp(gcode, "P92", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P92");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (programmingMode != ProgrammingMode::PROGRAMMING)
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		if (activeProgram->forLoopCounter != 0) {
			return ParsingGCodeResult::NON_CLOSED_LOOP;
		} else {
			activeProgram->reset();
			activeProgram = nullptr;
		}

		programmingMode.store(ProgrammingMode::NO_PROGRAMM);

	} else
		return ParsingGCodeResult::INVALID_COMMAND;

	return ParsingGCodeResult::SUCCESS;
}

void StepperControl::endstopHandler()
{
	if (stepperControl.programmingMode == ProgrammingMode::HOMING)
		xEventGroupSetBits(StepperControl::homingEventGroup, BIT0);
}

void StepperControl::home()
{
	ProgrammingMode mode = programmingMode.load();
	programmingMode.store(ProgrammingMode::HOMING);
	homeH();
	homeT();

	programmingMode.store(mode);
}

void StepperControl::homeH()
{
#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Homing H axis");
#endif /* CONFIG_APP_DEBUG */
	// stop the steppers
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.stopNowStepper(stepperHalH);
	// attach interrupts
	attachInterrupt(CONFIG_STEPPER_H_PIN_ENDSTOP, StepperControl::endstopHandler, CHANGE);

	steppers.spindleStepper(stepperHalH, 10, Direction::FORWARD);

	EventBits_t result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Horizontal stepper fast homed");
#endif /* CONFIG_APP_DEBUG */

	steppers.stepStepper(stepperHalH, -20, 10);
	vTaskDelay(150);
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.spindleStepper(stepperHalH, 4, Direction::FORWARD);
	result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

	steppers.stopStepper(stepperHalH);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Horizontal stepper slow homed");
#endif /* CONFIG_APP_DEBUG */

	// cleanup
	steppers.getStepsTraveledOfPrevCommand(stepperHalH);
	detachInterrupt(CONFIG_STEPPER_H_PIN_ENDSTOP);
}

void StepperControl::homeT()
{
#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Homing T axis");
#endif /* CONFIG_APP_DEBUG */
	// stop the steppers
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.stopNowStepper(stepperHalT);
	// attach interrupts

	attachInterrupt(CONFIG_STEPPER_T_PIN_ENDSTOP, StepperControl::endstopHandler, CHANGE);

	steppers.spindleStepper(stepperHalT, 10, Direction::FORWARD);

	EventBits_t result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Tilt stepper fast homed");
#endif /* CONFIG_APP_DEBUG */

	steppers.stepStepper(stepperHalT, -80, 20);
	vTaskDelay(1000);
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.spindleStepper(stepperHalT, 6, Direction::FORWARD);
	result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

	steppers.stopStepper(stepperHalT);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Tilt stepper slow homed");
#endif /* CONFIG_APP_DEBUG */

	// cleanup
	steppers.getStepsTraveledOfPrevCommand(stepperHalT);
	detachInterrupt(CONFIG_STEPPER_T_PIN_ENDSTOP);
}
