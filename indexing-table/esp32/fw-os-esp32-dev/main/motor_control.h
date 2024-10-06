/*
 * motor_control.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef MOTOR_CONTROLL_H
#define MOTOR_CONTROLL_H

#include <esp_log.h>
#include <per_stepper_driver.h>

#define RMP_TO_PAUSE(speed, steps_per_revolution, gear_ratio) (60L * 1000L * 1000L / this->stepCount / gear_ratio / whatSpeed) // ->
#define ANGLE_TO_STEP(angle, steps_per_revolution, gear_ratio) (angle * steps_per_revolution * gear_ratio / 360)
#define STEP_TO_ANGLE(step, steps_per_revolution, gear_ratio) (step * 360 / steps_per_revolution / gear_ratio)
#define ANGLE_DISTANCE(angle1, angle2) (angle1 > angle2 ? angle1 - angle2 : angle2 - angle1)

#define port_TICK_PERIOD_US portTICK_PERIOD_MS * 1000

enum PositioningMode : uint8_t {
	HOMING_FAST = 0,
	HOMING_SLOW = 1,
	ABSOLUTE = 2,
	RELATIVE = 3,
};

enum MotorMode : uint8_t {
	HOMING = 0,
	STOPPED = 1,
	SPINDLE_CLOCKWISE = 2,
	SPINDLE_COUNTERCLOCKWISE = 3
	STEPPER = 4,
};

typedef struct {
	// motor configuration paramters:
	int8_t endstop = -1; // endstop pin
	int16_t angleMax = -1; // maximum angle
	int16_t angleMin = -1; // minimum angle
	uint16_t gearRatio = 1; // gear ratio (only down gearing)
	uint8_t microstepping = 1; // microstepping
	uint16_t stepCount = 100; // steps per revolution

	// operational variables;
	MotorMode mode = MotorMode::STEPPER; // motor mode
	uint16_t angle; // current angle (always in absolute mode)
	uint16_t angleStartRelative; // angle at the beginning of the relative positioning
	int32_t stepsToGo; // number of steps to go
	uint32_t pause = 1000'0000; // default sleep roughly ones pers second
	uint64_t lastStepTime = 0; // time of the last step
}  __attribute__((packed)) motorVariables;

class MotorControl{
	private:
	constexpr static char TAG[] = "MotorControl";
	static MotorControl* instance;

	PositioningMode positioningMode = PositioningMode::ABSOLUTE;

	PerStepperDriver* horzMot = nullptr;
	PerStepperDriver* tiltMot = nullptr;
	volatile motorVariables horzVar;
	volatile motorVariables tiltVar;

	TaskHandle_t motorMoveTaskHandle = NULL;

	static void motorMoveTask(void *arg);


	void homeRoutine(uint8_t part);



	MotorControl();

	public:



	static MotorControl* getInstance()
	{
		if(instance == nullptr)
		{
			instance = new MotorControl();
		}
		return instance;
	};

	void setMotors(PerStepperDriver* horizontal, PerStepperDriver* tilt, uint8_t horizontalStepCount = 200, uint8_t tiltStepCount = 200, uint8_t horizontalGearRatio =1 , uint8_t tiltGearRatio = 1 , uint8_t horizontalMicrostepping =1 , uint8_t tiltMicrostepping = 1)
	{
		this->horizontal = horizontal;
		this->tilt = tilt;
		this->horzVar.stepCount = horizontalStepCount;
		this->tiltVar.stepCount = tiltStepCount;
		this->horzVar.gearRatio = horizontalGearRatio;
		this->tiltVar.gearRatio = tiltGearRatio;
		this->horzVar.microstepping = horizontalMicrostepping;
		this->tiltVar.microstepping = tiltMicrostepping;
		this->horzVar.endstop = horizontalEndstop;
		this->tiltVar.endstop = tiltEndstop;
		this->horzVar.angleMax = horizontalAngleMax;
		this->tiltVar.angleMax = tiltAngleMax;
		this->horzVar.angleMin = horizontalAngleMin;
		this->tiltVar.angleMin = tiltAngleMin;
	};

	void startMotorTask();

	void setEndstops(int8_t horizontal = -1, int8_t tilt = -1);

	PerStepperDriver* getHorizontal()
	{
		return horizontal;
	};

	PerStepperDriver* getTilt()
	{
		return tilt;
	};

	void parseGcode(const char* gcode, uint16_t length);

	void printLocation();

	void homeHorz(){
		homeRoutine(0);
	}


};

#endif /* !MOTOR_CONTROLL_H */
