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

	pinMode(CONFIG_STEPPER_Y_PIN_EN, OUTPUT);
	pinMode(CONFIG_STEPPER_P_PIN_EN, OUTPUT);
	ESP_LOGI(TAG, "commandSchedulerTask | stepper Y\n\tenable pin %d\n\tendstop pin %d\n\tstep pin %d\n\tsense pin %d\n\tdirection pin %d", CONFIG_STEPPER_Y_PIN_EN, CONFIG_STEPPER_Y_PIN_ENDSTOP, CONFIG_STEPPER_Y_PIN_STEP, CONFIG_STEPPER_Y_PIN_SENSE, CONFIG_STEPPER_Y_PIN_DIR);
	ESP_LOGI(TAG, "commandSchedulerTask | stepper P\n\tenable pin %d\n\tendstop pin %d\n\tstep pin %d\n\tsense pin %d\n\tdirection pin %d", CONFIG_STEPPER_P_PIN_EN, CONFIG_STEPPER_P_PIN_ENDSTOP, CONFIG_STEPPER_P_PIN_STEP, CONFIG_STEPPER_P_PIN_SENSE, CONFIG_STEPPER_P_PIN_DIR);
	gpio_set_level((gpio_num_t)CONFIG_STEPPER_Y_PIN_EN, 0);

	gpio_set_level((gpio_num_t)CONFIG_STEPPER_P_PIN_EN, 0);

	pinMode(CONFIG_STEPPER_Y_PIN_ENDSTOP, INPUT);
	pinMode(CONFIG_STEPPER_P_PIN_ENDSTOP, INPUT);

	noProgrammQueueLock = xSemaphoreCreateMutex();

	if (noProgrammQueueLock == NULL) {
		ESP_LOGE(TAG, "JoPkaEndpoint | xSemaphoreCreateMutex failed");
	} else {
		ESP_LOGI(TAG, "JoPkaEndpoint | created lock");
	}
	// on first step there are some initializations taking place that make it much longer than the rest
	steppers.stepStepper(stepperHalPitch, 1, 1, true);
	steppers.stepStepper(stepperHalYaw, 1, 1, true);

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

				// fix cases where matchString="P" will match "SP" and fail
				if (str[i - 1] >= 'A' && str[i - 1] <= 'Z')
					continue;

				i += elementLength;
				// fix cases where matchString="S" will match "SP" and fail
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

				// fix cases where matchString="P" will match "SP" and fail
				if (str[i - 1] >= 'A' && str[i - 1] <= 'Z')
					continue;

				i += elementLength;
				// fix cases where matchString="S" will match "SP" and fail
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
		ESP_LOGI(TAG, "moveStepperAbsolute | %s no limits", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "Y" : "P");
#endif
		steppers.stepStepper(stepperHalYaw, ANGLE_DISTANCE(stepperOpPar->positionLastScheduled, movement->val.steps, stepperHal->stepCount), movement->rpm, synchronized);
	} else if (stepperOpPar->stepsMax >= stepperOpPar->stepsMin) { // moving in interval <min, max>
#ifdef CONFIG_APP_DEBUG
		ESP_LOGI(TAG, "moveStepperAbsolute | %s in interval <min, max>", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "Y" : "P");
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
		ESP_LOGI(TAG, "moveStepperAbsolute | %s in interval <0, max> U <min, stepCount>", stepperHal->stepperCompleteBit == STEPPER_COMPLETE_BIT_H ? "Y" : "P");
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
	stepper_operation_paramters_t stepperOpParYaw;
	stepper_operation_paramters_t stepperOpParPitch;
	Unit unit = Unit::STEPS; // only used in motorTask

	gcode_command_t* command = nullptr; // NOTE: this is just hack so I don't have tu turn off -Werror=maybe-uninitialized

	// checks order of from and dest when in union of intervals <0, min> U <max, stepCount>
	// uint8_t  tmp = 0;
	// true if from comes before dest -> we need to move clockwise

	while (true) {
		stepperOpParYaw.position += steppers.getStepsTraveledOfPrevCommand(stepperHalYaw);
		stepperOpParPitch.position += steppers.getStepsTraveledOfPrevCommand(stepperHalPitch);


		// tmp++;
		// if(tmp == 20){
		// 	tmp =0;
		// 	int64_t travelledH = steppers.getStepsTraveledOfCurrentCommand(stepperHalYaw);
		// 	int64_t travelledT = steppers.getStepsTraveledOfCurrentCommand(stepperHalPitch);
		// 	ESP_LOGI(TAG, "H position %lld, H traveled %lld, T position %lld, T traveled %lld", stepperOpParYaw.position, travelledH, stepperOpParPitch.position, travelledT);
		// 	ESP_LOGI(TAG, "!P %lld, %f, %f\n", esp_timer_get_time()/1000, STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParYaw.position + travelledH, stepperHalYaw->stepCount), stepperHalYaw->stepCount), STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParPitch.position + travelledT, stepperHalYaw->stepCount), stepperHalYaw->stepCount));
		// }


		printf("!P %lld, %f, %f\n", esp_timer_get_time()/1000, STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParYaw.position + steppers.getStepsTraveledOfCurrentCommand(stepperHalYaw), stepperHalYaw->stepCount), stepperHalYaw->stepCount), STEPS_TO_ANGLE(NORMALIZE_ANGLE(stepperOpParPitch.position + steppers.getStepsTraveledOfCurrentCommand(stepperHalPitch), stepperHalPitch->stepCount), stepperHalPitch->stepCount));
		// if queues are filled we will wait

		if (steppers.getQueueLength(stepperHalYaw) == CONFIG_STEPPER_YAL_QUEUE_SIZE || steppers.getQueueLength(stepperHalPitch) == CONFIG_STEPPER_YAL_QUEUE_SIZE) {
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
				steppers.stopStepper(stepperHalYaw);
				steppers.stopStepper(stepperHalPitch);
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
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G90 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G90 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G90 | P");
#endif
			if (command->movementYaw != nullptr)
				stepperOpParYaw.positioningMode = PositioningMode::ABSOLUTE;
			if (command->movementPitch != nullptr)
				stepperOpParPitch.positioningMode = PositioningMode::ABSOLUTE;
			break;
		case GCodeCommand::G91:
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G91 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G91 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G91 | P");
#endif
			if (command->movementYaw != nullptr)
				stepperOpParYaw.positioningMode = PositioningMode::RELATIVE;
			if (command->movementPitch != nullptr)
				stepperOpParPitch.positioningMode = PositioningMode::RELATIVE;
			break;
		case GCodeCommand::G92:
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G92 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G92 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G92 | P");
#endif
			if (command->movementYaw != nullptr) {
				if (steppers.getQueueLength(stepperHalYaw) != 0) {
					ESP_LOGE(TAG, "commandSchedulerTask | G92 | ERR: cannot reset position while there are commands in queue");
				} else {
					steppers.getStepsTraveledOfPrevCommand(stepperHalYaw); // clear previous command steps
					stepperOpParYaw.position = 0;
					stepperOpParYaw.positionLastScheduled = 0;
				}
			}
			if (command->movementPitch != nullptr) {
				if (steppers.getQueueLength(stepperHalPitch) != 0) {
					ESP_LOGE(TAG, "commandSchedulerTask | G92 | ERR: cannot reset position while there are commands in queue");
				} else {
					steppers.getStepsTraveledOfPrevCommand(stepperHalPitch); // clear previous command steps
					stepperOpParPitch.position = 0;
					stepperOpParPitch.positionLastScheduled = 0;
				}
			}

			break;
		case GCodeCommand::G28: {
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G28 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G28 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | G28 | P");
#endif
			ProgrammingMode mode = programmingMode.load();
			programmingMode.store(ProgrammingMode::HOMING);
			if (command->movementYaw != nullptr) {
				stepperControl.homeYaw();
			}
			if (command->movementPitch != nullptr) {
				stepperControl.homePitch();
			}
			programmingMode.store(mode);
		 break;
														}
		case GCodeCommand::G0:
			if (command->movementYaw != nullptr) {
				if (command->movementYaw->val.steps == 0)
					steppers.skipStepper(stepperHalYaw, SYNCHRONIZED);
				command->movementYaw->val.steps = unit == Unit::DEGREES ? ANGLE_TO_STEPS(command->movementYaw->val.steps, stepperHalYaw->stepCount) : command->movementYaw->val.steps;
				if (stepperOpParYaw.positioningMode == PositioningMode::ABSOLUTE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | Y absolute");
#endif
					command->movementYaw->val.steps = NORMALIZE_ANGLE(command->movementYaw->val.steps, stepperHalYaw->stepCount);
					stepperOpParYaw.positionLastScheduled = moveStepperAbsolute(stepperHalYaw, command->movementYaw, &stepperOpParYaw, SYNCHRONIZED);
				} else {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | Y relative");
#endif
					stepperOpParYaw.positionLastScheduled = moveStepperRelative(stepperHalYaw, command->movementYaw, &stepperOpParYaw, SYNCHRONIZED);
				}
			}
			if (command->movementPitch != nullptr) {
				if (command->movementPitch->val.steps == 0)
					steppers.skipStepper(stepperHalPitch, SYNCHRONIZED);
				command->movementPitch->val.steps = unit == Unit::DEGREES ? ANGLE_TO_STEPS(command->movementPitch->val.steps, stepperHalPitch->stepCount) : command->movementPitch->val.steps;
				if (stepperOpParPitch.positioningMode == PositioningMode::ABSOLUTE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | P absolute");
#endif
					command->movementPitch->val.steps = NORMALIZE_ANGLE(command->movementPitch->val.steps, stepperHalPitch->stepCount);
					stepperOpParPitch.positionLastScheduled = moveStepperAbsolute(stepperHalPitch, command->movementPitch, &stepperOpParPitch, SYNCHRONIZED);
				} else {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGI(TAG, "commandSchedulerTask | G0 | P relative");
#endif
					stepperOpParPitch.positionLastScheduled = moveStepperRelative(stepperHalPitch, command->movementPitch, &stepperOpParPitch, SYNCHRONIZED);
				}
			}
			break;
		case GCodeCommand::M03:
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M03 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M03 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M03 | P");
#endif
			if (command->movementYaw != nullptr) {
				if (stepperOpParYaw.positioningMode != PositioningMode::RELATIVE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGE(TAG, "commandSchedulerTask | M03 | Y ERR: command with absolute positioning is not supported, will switch to relative");
#endif
					stepperOpParYaw.positioningMode = PositioningMode::RELATIVE;
				}
				steppers.spindleStepper(stepperHalYaw, command->movementYaw->rpm, command->movementYaw->val.direction);
			}
			if (command->movementPitch != nullptr) {
				if (stepperOpParPitch.positioningMode != PositioningMode::RELATIVE) {
#ifdef CONFIG_APP_DEBUG
					ESP_LOGE(TAG, "commandSchedulerTask | M03 | P ERR: command with absolute positioning is not supported, will switch to relative");
#endif
					stepperOpParPitch.positioningMode = PositioningMode::RELATIVE;
				}
				steppers.spindleStepper(stepperHalPitch, command->movementPitch->rpm, command->movementPitch->val.direction);
			}
			break;
		case GCodeCommand::M05:
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M05 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M05 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M05 | P");
#endif
			if (command->movementYaw != nullptr)
				steppers.stopStepper(stepperHalYaw, SYNCHRONIZED);
			if (command->movementPitch != nullptr)
				steppers.stopStepper(stepperHalPitch, SYNCHRONIZED);
			break;
		case GCodeCommand::M201:
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M201 | Y: min %ld, max %ld | P: min %ld, max %ld", stepperOpParYaw.stepsMin, stepperOpParYaw.stepsMax, stepperOpParPitch.stepsMin, stepperOpParPitch.stepsMax);
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M201 | Y: min %ld, max %ld", stepperOpParYaw.stepsMin, stepperOpParYaw.stepsMax);
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M201 | P: min %ld, max %ld", stepperOpParPitch.stepsMin, stepperOpParPitch.stepsMax);
#endif
			if (unit == Unit::STEPS) {
				if (command->movementYaw != nullptr) {
					stepperOpParYaw.stepsMin = command->movementYaw->val.limits.min <= stepperHalYaw->stepCount ? (uint32_t)command->movementYaw->val.limits.min : stepperHalYaw->stepCount;
					stepperOpParYaw.stepsMax = command->movementYaw->val.limits.max <= stepperHalYaw->stepCount ? (uint32_t)command->movementYaw->val.limits.max : stepperHalYaw->stepCount;
				}
				if (command->movementPitch != nullptr) {
					stepperOpParPitch.stepsMin = command->movementPitch->val.limits.min <= stepperHalPitch->stepCount ? (uint32_t)command->movementPitch->val.limits.min : stepperHalPitch->stepCount;
					stepperOpParPitch.stepsMax = command->movementPitch->val.limits.max <= stepperHalPitch->stepCount ? (uint32_t)command->movementPitch->val.limits.max : stepperHalPitch->stepCount;
				}
			} else {
				if (command->movementYaw != nullptr) {
					stepperOpParYaw.stepsMin = ANGLE_TO_STEPS(command->movementYaw->val.limits.min, stepperHalYaw->stepCount) <= stepperHalYaw->stepCount ? ANGLE_TO_STEPS(command->movementYaw->val.limits.min, stepperHalYaw->stepCount) : stepperHalYaw->stepCount;
					stepperOpParYaw.stepsMax = ANGLE_TO_STEPS(command->movementYaw->val.limits.max, stepperHalYaw->stepCount) <= stepperHalYaw->stepCount ? ANGLE_TO_STEPS(command->movementYaw->val.limits.max, stepperHalYaw->stepCount) : stepperHalYaw->stepCount;
				}
				if (command->movementPitch != nullptr) {
					stepperOpParPitch.stepsMin = ANGLE_TO_STEPS(command->movementPitch->val.limits.min, stepperHalYaw->stepCount) <= stepperHalPitch->stepCount ? ANGLE_TO_STEPS(command->movementPitch->val.limits.min, stepperHalYaw->stepCount) : stepperHalPitch->stepCount;
					stepperOpParPitch.stepsMax = ANGLE_TO_STEPS(command->movementPitch->val.limits.max, stepperHalYaw->stepCount) <= stepperHalPitch->stepCount ? ANGLE_TO_STEPS(command->movementPitch->val.limits.max, stepperHalYaw->stepCount) : stepperHalPitch->stepCount;
				}
			}
			break;

		case GCodeCommand::M202:
#ifdef CONFIG_APP_DEBUG
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M202 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M202 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | M202 | P");
#endif
			if (command->movementYaw != nullptr) {
				stepperOpParYaw.stepsMin = GCODE_ELEMENT_INVALID_INT;
				stepperOpParYaw.stepsMax = GCODE_ELEMENT_INVALID_INT;
			}
			if (command->movementPitch != nullptr) {
				stepperOpParPitch.stepsMin = GCODE_ELEMENT_INVALID_INT;
				stepperOpParPitch.stepsMax = GCODE_ELEMENT_INVALID_INT;
			}
			break;
		case GCodeCommand::P21:
#ifdef CONFIG_APP_DEBUG
			ESP_LOGI(TAG, "commandSchedulerTask | P21 | %d", activeProgram->forLoopCounter);
#endif
			activeProgram->forLoopCounter = command->movementYaw->val.iterations;
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
			if (command->movementYaw != nullptr && command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | Y, P");
			else if (command->movementYaw != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | Y");
			else if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W1 | P");
#endif
			if (command->movementYaw != nullptr) {
				steppers.waitStepper(stepperHalYaw, command->movementYaw->val.time, SYNCHRONIZED);
			}
			if (command->movementPitch != nullptr) {
				steppers.waitStepper(stepperHalPitch, command->movementPitch->val.time, SYNCHRONIZED);
			}
			break;
		case GCodeCommand::W3:
#ifdef CONFIG_APP_DEBUG
			if (command->movementPitch != nullptr)
				ESP_LOGI(TAG, "commandSchedulerTask | W3");
#endif
			if (command->movementPitch != nullptr) {
				vTaskDelay(command->movementPitch->val.time / portTICK_PERIOD_MS);
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
		steppers.stopNowStepper(stepperHalYaw);
		steppers.stopNowStepper(stepperHalPitch);
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_Y_PIN_EN, 0);
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_P_PIN_EN, 0);

		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "M81", 3) == 0) { // power up high voltage supply
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode | M81");
#endif /* CONFIG_COMM_DEBUG */
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_Y_PIN_EN, 1);
		gpio_set_level((gpio_num_t)CONFIG_STEPPER_P_PIN_EN, 1);
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
		steppers.stopNowStepper(stepperHalYaw);
		steppers.stopNowStepper(stepperHalPitch);
		return ParsingGCodeResult::SUCCESS;
	} else if (strncmp(gcode, "G3", 2) == 0) { // override
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G3");
#endif /* CONFIG_COMM_DEBUG */
		gcode_command_movement_t* movementYaw = nullptr;
		gcode_command_movement_t* movementPitch = nullptr;
		elementInt = getElementInt(gcode, length, 3, "Y", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			movementYaw = new gcode_command_movement_t();
			movementYaw->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementInt = getElementInt(gcode, length, 3, "P", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			movementPitch = new gcode_command_movement_t();
			movementPitch->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementFloat = getElementFloat(gcode, length, 3, "S", 1);
		if (!std::isnan(elementFloat)) {
			if (movementYaw != nullptr)
				movementYaw->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			if (movementPitch != nullptr) {
				movementPitch->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			}
		}

		elementFloat = getElementFloat(gcode, length, 3, "SY", 2);
		if (!std::isnan(elementFloat) && movementYaw != nullptr)
			movementYaw->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);

		elementFloat = getElementFloat(gcode, length, 3, "SP", 2);
		if (!std::isnan(elementFloat) && movementPitch != nullptr) {
			movementPitch->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
		}

		if ((movementYaw != nullptr && movementYaw->rpm == NAN && movementYaw->val.steps != 0) || (movementPitch != nullptr && movementPitch->rpm == NAN && movementPitch->val.steps != 0))
			return ParsingGCodeResult::INVALID_ARGUMENT;

		if(movementYaw == nullptr && movementPitch == nullptr)
			return ParsingGCodeResult::INVALID_ARGUMENT;

		if (movementYaw != nullptr && movementPitch != nullptr) {
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| G3 | Y: %d, P: %d, SY: %f, ST %f", movementYaw->val.steps, movementPitch->val.steps, movementYaw->rpm, movementPitch->rpm);
#endif /* CONFIG_COMM_DEBUG */
			steppers.stepStepper(stepperHalYaw, movementYaw->val.steps, movementYaw->rpm, true);
			steppers.stepStepper(stepperHalYaw, movementPitch->val.steps, movementPitch->rpm, true);
			return ParsingGCodeResult::SUCCESS;
		}
		if (movementYaw != nullptr) {
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| G3 | Y: %d, SY: %f", movementYaw->val.steps, movementYaw->rpm);
#endif /* CONFIG_COMM_DEBUG */
			steppers.stepStepper(stepperHalYaw, movementYaw->val.steps, movementYaw->rpm, false);
		}
		if (movementPitch != nullptr) {
#ifdef CONFIG_COMM_DEBUG
			ESP_LOGI(TAG, "parseGCode| G3 | P: %d, SP: %f", movementPitch->val.steps, movementPitch->rpm);
#endif /* CONFIG_COMM_DEBUG */
			steppers.stepStepper(stepperHalPitch, movementPitch->val.steps, movementPitch->rpm, false);
		}
		return ParsingGCodeResult::SUCCESS;

	} else if (strncmp(gcode, "M92", 3) == 0) { // set steps per unit
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M92");
#endif /* CONFIG_COMM_DEBUG */
		elementInt = getElementInt(gcode, length, 4, "Y", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0)
			stepperHalYaw->stepCount = elementInt;
		else
			return ParsingGCodeResult::INVALID_ARGUMENT;

		elementInt = getElementInt(gcode, length, 4, "P", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0)
			stepperHalPitch->stepCount = elementInt;
		else
			return ParsingGCodeResult::INVALID_ARGUMENT;

#ifdef CONFIG_COMM_DEBUG
		fprintf(stderr, "parseGCode| M92 | Y: %d, P: %d\n", stepperHalYaw->stepCount, stepperHalPitch->stepCount);
#endif /* CONFIG_COMM_DEBUG */
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
		steppers.stopStepper(stepperHalYaw);
		steppers.stopStepper(stepperHalPitch);
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
		if (!getElementString(gcode, length, 3, "Y", 1) && !getElementString(gcode, length, 3, "P", 1)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementPitch = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "Y", 1)) {
			command->movementYaw = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "P", 1)) {
			command->movementPitch = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G91", 3) == 0) { // set the relative positioning
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G91");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G91;
		if (!getElementString(gcode, length, 3, "Y", 1) && !getElementString(gcode, length, 3, "P", 1)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementPitch = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "Y", 1)) {
			command->movementYaw = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "P", 1)) {
			command->movementPitch = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G92", 3) == 0) { // set current position as home
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G92");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G92;
		if (!getElementString(gcode, length, 3, "Y", 1) && !getElementString(gcode, length, 3, "P", 1)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementPitch = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "Y", 1)) {
			command->movementYaw = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "P", 1)) {
			command->movementPitch = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G28", 3) == 0) { // home both drivers
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G28");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G28;
		if (!getElementString(gcode, length, 3, "Y", 1) && !getElementString(gcode, length, 3, "P", 1)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementPitch = new gcode_command_movement_t();
		} else if (getElementString(gcode, length, 3, "Y", 1)) {
			command->movementYaw = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 3, "P", 1)) {
			command->movementPitch = new gcode_command_movement_t();
		}
	} else if (strncmp(gcode, "G0", 2) == 0) { // home to given position, not the most efficient parsing but we don't excpet to have that many commands to process
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| G0");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::G0;
		elementInt = getElementInt(gcode, length, 3, "Y", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementInt = getElementInt(gcode, length, 3, "P", 1);
		if (elementInt != GCODE_ELEMENT_INVALID_INT) {
			command->movementPitch = new gcode_command_movement_t();
			command->movementPitch->val.steps = LIMIT_NUMBER(elementInt, INT16_MIN, INT16_MAX);
		}

		elementFloat = getElementFloat(gcode, length, 3, "S", 1);
		if (!std::isnan(elementFloat)) {
			if (command->movementYaw != nullptr)
				command->movementYaw->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			if (command->movementPitch != nullptr) {
				command->movementPitch->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
			}
		}

		elementFloat = getElementFloat(gcode, length, 3, "SY", 2);
		if (!std::isnan(elementFloat) && command->movementYaw != nullptr)
			command->movementYaw->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);

		elementFloat = getElementFloat(gcode, length, 3, "SP", 2);
		if (!std::isnan(elementFloat) && command->movementPitch != nullptr) {
			command->movementPitch->rpm = LIMIT_NUMBER(elementFloat, 0, CONFIG_STEPPER_MAX_SPEED);
		}

		if ((command->movementYaw != nullptr && command->movementYaw->rpm == NAN && command->movementYaw->val.steps != 0) || (command->movementPitch != nullptr && command->movementPitch->rpm == NAN && command->movementPitch->val.steps != 0))
			return ParsingGCodeResult::INVALID_ARGUMENT;

		if(command->movementYaw == nullptr && command->movementPitch == nullptr)
			return ParsingGCodeResult::INVALID_ARGUMENT;

#ifdef CONFIG_COMM_DEBUG
		if (command->movementYaw != nullptr && command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| G0 | Y: %d, P: %d, SY: %f, ST %f", command->movementYaw->val.steps, command->movementPitch->val.steps, command->movementYaw->rpm, command->movementPitch->rpm);
		else if (command->movementYaw != nullptr)
			ESP_LOGI(TAG, "parseGCode| G0 | Y: %d, SY: %f", command->movementYaw->val.steps, command->movementYaw->rpm);
		else if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| G0 | P: %d, SP: %f", command->movementPitch->val.steps, command->movementPitch->rpm);
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

		if (getElementString(gcode, length, 3, "Y+", 2)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.direction = Direction::FORWARD;
		} else if (getElementString(gcode, length, 3, "Y-", 2)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.direction = Direction::BACKWARD;
		}

		if (getElementString(gcode, length, 3, "P+", 2)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.direction = Direction::FORWARD;
		} else if (getElementString(gcode, length, 3, "P-", 2)) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.direction = Direction::BACKWARD;
		}

		elementFloat = getElementFloat(gcode, length, 3, "S", 1);
		if (!std::isnan(elementFloat)) {
			if (command->movementYaw != nullptr)
				command->movementYaw->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
			if (command->movementPitch != nullptr)
				command->movementPitch->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		}

		elementFloat = getElementFloat(gcode, length, 3, "SY", 2);
		if (!std::isnan(elementFloat) && command->movementYaw != nullptr) {
			command->movementYaw->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		}

		elementFloat = getElementFloat(gcode, length, 3, "SP", 2);
		if (!std::isnan(elementFloat) && command->movementPitch != nullptr) {
			command->movementPitch->rpm = elementFloat < CONFIG_STEPPER_MAX_SPEED ? elementFloat : CONFIG_STEPPER_MAX_SPEED;
		}

		if ((command->movementYaw != nullptr && command->movementYaw->rpm == NAN) || (command->movementPitch != nullptr && command->movementPitch->rpm == NAN))
			return ParsingGCodeResult::INVALID_ARGUMENT;

#ifdef CONFIG_COMM_DEBUG
		if (command->movementYaw != nullptr && command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| M03 | Y: %d, P: %d, SY: %f, ST %f", command->movementYaw->val.direction, command->movementPitch->val.direction, command->movementYaw->rpm, command->movementPitch->rpm);
		else if (command->movementYaw != nullptr)
			ESP_LOGI(TAG, "parseGCode| M03 | Y: %d, SY: %f", command->movementYaw->val.direction, command->movementYaw->rpm);
		else if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| M03 | P: %d, SP: %f", command->movementPitch->val.direction, command->movementPitch->rpm);

#endif /* CONFIG_COMM_DEBUG */
	} else if (strncmp(gcode, "M05", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M05");
#endif /* CONFIG_COMM_DEBUG */

		command->type = GCodeCommand::M05;
		if (getElementString(gcode, length, 3, "Y", 1)) {
			command->movementYaw = new gcode_command_movement_t(); // stepper will stop in command->movementPitch is not nullptr
		}

		if (getElementString(gcode, length, 3, "P", 1)) {
			command->movementPitch = new gcode_command_movement_t(); // stepper will stop in command->movementPitch is not nullptr
		}
	} else if (strncmp(gcode, "M201", 4) == 0) { // set limits for the steppers
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M201");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::M201;
		if (getElementString(gcode, length, 5, "LY", 2) && getElementString(gcode, length, 5, "HY", 2)) {
			command->movementYaw = new gcode_command_movement_t();
			elementFloat = getElementFloat(gcode, length, 5, "LY", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementYaw->val.limits.min = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;

			elementFloat = getElementFloat(gcode, length, 5, "HY", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementYaw->val.limits.max = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;
		}

		if (getElementString(gcode, length, 5, "LP", 2) && getElementString(gcode, length, 5, "HP", 2)) {
			command->movementPitch = new gcode_command_movement_t();

			elementFloat = getElementFloat(gcode, length, 5, "LP", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementPitch->val.limits.min = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;

			elementFloat = getElementFloat(gcode, length, 5, "HP", 2);
			if (!std::isnan(elementFloat) && elementFloat >= 0) {
				command->movementPitch->val.limits.max = elementFloat;
			} else
				return ParsingGCodeResult::INVALID_ARGUMENT;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementYaw != nullptr && command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| M201 | Y: [%f, %f] P: [%f, %f]", command->movementYaw->val.limits.min, command->movementYaw->val.limits.max, command->movementPitch->val.limits.min, command->movementPitch->val.limits.max);
		else if (command->movementYaw != nullptr)
			ESP_LOGI(TAG, "parseGCode| M201 | Y: %f, SY: %f", command->movementYaw->val.limits.min, command->movementYaw->val.limits.max);
		else if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| M201 | P: %f, SP: %f", command->movementPitch->val.limits.min, command->movementPitch->val.limits.max);
#endif /* CONFIG_COMM_DEBUG */

	} else if (strncmp(gcode, "M202", 4) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| M202");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::M202;
		if (getElementString(gcode, length, 5, "Y", 1)) {
			command->movementYaw = new gcode_command_movement_t();
		}
		if (getElementString(gcode, length, 5, "P", 1)) {
			command->movementPitch = new gcode_command_movement_t();
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementYaw != nullptr && command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| M202 | Y, P");
		else if (command->movementYaw != nullptr)
			ESP_LOGI(TAG, "parseGCode| M202 | Y");
		else if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| M202 | P");
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
		elementInt = getElementInt(gcode, length, 2, "Y", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.time = elementInt * 1000;
		}

		elementInt = getElementInt(gcode, length, 2, "P", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementPitch = new gcode_command_movement_t();
			command->movementPitch->val.time = elementInt * 1000;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementYaw != nullptr && command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| W0 | Y: %ld, P: %ld", command->movementYaw->val.time, command->movementPitch->val.time);
		else if (command->movementYaw != nullptr)
			ESP_LOGI(TAG, "parseGCode| W0 | Y: %ld", command->movementYaw->val.time);
		else if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| W0 | P: %ld", command->movementPitch->val.time);
#endif /* CONFIG_COMM_DEBUG */
	} else if (strncmp(gcode, "W1", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| W1");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::W1;
		elementInt = getElementInt(gcode, length, 2, "Y", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.time = elementInt;
		}

		elementInt = getElementInt(gcode, length, 2, "P", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementPitch = new gcode_command_movement_t();
			command->movementPitch->val.time = elementInt;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementYaw != nullptr && command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| W1 | Y: %ld, P: %ld", command->movementYaw->val.time, command->movementPitch->val.time);
		else if (command->movementYaw != nullptr)
			ESP_LOGI(TAG, "parseGCode| W1 | Y: %ld", command->movementYaw->val.time);
		else if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| W1 | P: %ld", command->movementPitch->val.time);
#endif /* CONFIG_COMM_DEBUG */;
	} else if (strncmp(gcode, "W3", 2) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| W3");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::W3;
		elementInt = getElementInt(gcode, length, 2, "T", 1);

		if (elementInt != GCODE_ELEMENT_INVALID_INT && elementInt > 0) {
			command->movementPitch = new gcode_command_movement_t();
			command->movementPitch->val.time = elementInt;
		}
#ifdef CONFIG_COMM_DEBUG
		if (command->movementPitch != nullptr)
			ESP_LOGI(TAG, "parseGCode| W3 | P: %ld", command->movementPitch->val.time);
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
			command->movementYaw = new gcode_command_movement_t();
			command->movementYaw->val.iterations = elementInt;
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
		steppers.stopNowStepper(stepperHalPitch);
		steppers.stopNowStepper(stepperHalYaw);
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
		if (programmingMode != ProgrammingMode::PROGRAMMING || commandDestination != activeProgram->header)
			return ParsingGCodeResult::COMMAND_BAD_CONTEXT;
		if (activeProgram->header->size() == 0)
			return ParsingGCodeResult::NON_CLOSED_LOOP;

		commandDestination = activeProgram->main;
	} else if (strncmp(gcode, "P29", 3) == 0) {
#ifdef CONFIG_COMM_DEBUG
		ESP_LOGI(TAG, "parseGCode| P29");
#endif /* CONFIG_COMM_DEBUG */
		command->type = GCodeCommand::COMMAND_TO_REMOVE;
		if (programmingMode != ProgrammingMode::PROGRAMMING || commandDestination != activeProgram->header) // we can only declare looped command in the header
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
	xEventGroupSetBits(StepperControl::homingEventGroup, BIT0);
}

void StepperControl::home()
{
	ProgrammingMode mode = programmingMode.load();
	programmingMode.store(ProgrammingMode::HOMING);
	homeYaw();
	homePitch();

	programmingMode.store(mode);
}

void StepperControl::homeYaw()
{
#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Homing yaw");
#endif /* CONFIG_APP_DEBUG */
	// stop the steppers
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.stopNowStepper(stepperHalYaw);
	// attach interrupts
	attachInterrupt(CONFIG_STEPPER_Y_PIN_ENDSTOP, StepperControl::endstopHandler, CHANGE);

	steppers.spindleStepper(stepperHalYaw, 10, Direction::FORWARD);

	EventBits_t result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Yaw stepper fast homed");
#endif /* CONFIG_APP_DEBUG */

	steppers.stepStepper(stepperHalYaw, -20, 6);
	vTaskDelay(150);
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.spindleStepper(stepperHalYaw, 3, Direction::FORWARD);
	result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

	steppers.stopStepper(stepperHalYaw);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Yaw stepper slow homed");
#endif /* CONFIG_APP_DEBUG */

	// cleanup
	steppers.getStepsTraveledOfPrevCommand(stepperHalYaw);
	detachInterrupt(CONFIG_STEPPER_Y_PIN_ENDSTOP);
}

void StepperControl::homePitch()
{
#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Homing pitch");
#endif /* CONFIG_APP_DEBUG */
	// stop the steppers
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.stopNowStepper(stepperHalPitch);
	// attach interrupts

	attachInterrupt(CONFIG_STEPPER_P_PIN_ENDSTOP, StepperControl::endstopHandler, CHANGE);

	steppers.spindleStepper(stepperHalPitch, 6, Direction::FORWARD);

	EventBits_t result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Pitch stepper fast homed");
#endif /* CONFIG_APP_DEBUG */

	steppers.stepStepper(stepperHalPitch, -30, 20);
	vTaskDelay(1000);
	xEventGroupClearBits(homingEventGroup, BIT0);
	steppers.spindleStepper(stepperHalPitch, 3, Direction::FORWARD);
	result = xEventGroupWaitBits(
			homingEventGroup,
			BIT0,
			pdTRUE,
			pdTRUE,
			portMAX_DELAY);

	steppers.stopStepper(stepperHalPitch);

#ifdef CONFIG_APP_DEBUG
	ESP_LOGI(TAG, "Home | Pitch stepper slow homed");
#endif /* CONFIG_APP_DEBUG */

	// cleanup
	steppers.getStepsTraveledOfPrevCommand(stepperHalPitch);
	detachInterrupt(CONFIG_STEPPER_P_PIN_ENDSTOP);
}
