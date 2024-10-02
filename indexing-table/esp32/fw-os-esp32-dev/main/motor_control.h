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
	ABSOLUTE = 0,
	RELATIVE = 1,
};

enum MotorMode : uint8_t {
	SPINDLE_CLOCKWISE = 0,
	SPINDLE_CLOCKWISE = 0,
	STEPPER = 1,
};

typedef struct {
	MotorMode mode = MotorMode::STEPPER; // motor mode
	int16_t angleMax = -1; // maximum angle
	int16_t angleMin = -1; // minimum angle
	uint16_t angle; // current angle (always in absolute mode)
	uint16_t angleStartRelative; // angle at the beginning of the relative positioning
	uint16_t stepsToGo; // number of steps to go
	uint32_t pause = 1000'0000; // default sleep roughly ones pers second
	uint64_t lastStepTime = 0; // time of the last step
}  __attribute__((packed)) motorVariables;

class MotorControl{
	private:
	constexpr static char TAG[] = "MotorControl";
	static MotorControl* instance;

	PerStepperDriver* horizontal = nullptr;
	PerStepperDriver* tilt = nullptr;
	PositioningMode positioningMode = PositioningMode::ABSOLUTE;

	volatile motorVariables horizontalMotorVariables;
	volatile motorVariables tiltMotorVariables;

	TaskHandle_t motorMoveTaskHandle = NULL;

	static void motorMoveTask(void *arg);





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

	void setMotors(PerStepperDriver* horizontal, PerStepperDriver* tilt)
	{
		this->rotation = rotation;
		this->tilt = tilt;
	};

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


};

#endif /* !MOTOR_CONTROLL_H */
