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
#include <callback_interface.h>
#include <os_core_tasker_ids.h>
#include <tasker_singleton_wrapper.h>


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
	uint16_t stepCount = 100; // steps per revolution

	// fault variables
	bool fault = false; // fault flag
	uint64_t faultTime = 0; // time of the fault

	// operational variables;
	MotorMode mode = MotorMode::STEPPER; // motor mode
	uint16_t angle; // current angle (always in absolute mode)
	uint16_t angleStartRelative; // angle at the beginning of the relative positioning
	int32_t stepsToGo; // number of steps to go
	uint32_t pause = 1000'0000; // default sleep roughly ones pers second
	uint64_t lastStepTime = 0; // time of the last step
}  __attribute__((packed)) motorVariables;

class MotorControl : public CallbackInterface{
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
	static void tiltEndstopHandler(void *arg);
	static void horizontalEndstopHandler(void *arg);

	void TiltMotorFault();
	void HorzMotorFault();



	/**
	 * @brief Home routine
	 * ought to not be called with other paramter than 0 by the user
	 *
	 * @param part 0 for starts fast homing, part 1 for backs off and home slowly, part 3 restores pre home configuration
	 */
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

	/**
   * @brief Set the Motors object
	 * For step count multiply the number of steps with your microstepping configuration
	 *
	 * @param horizontal horizontal motor pointer
	 * @param tilt tilt motor pointer
	 * @param horizontalStepCount steps per revolution of the horizontal motor
	 * @param tiltStepCount steps per revolution of the tilt motor
	 * @param horizontalGearRatio gear ratio of the horizontal motor
	 * @param tiltGearRatio gear ratio of the tilt motor
	 */
	void setMotors(PerStepperDriver* horizontal, PerStepperDriver* tilt, uint8_t horizontalStepCount = 200, uint8_t tiltStepCount = 200, uint8_t horizontalGearRatio =1 , uint8_t tiltGearRatio = 1);

	void setEndstops(int8_t horizontal = -1, int8_t tilt = -1);

	uint8_t call(uint16_t id) override;

	/**
	 * @brief parses incoming gcode and prepares its execution
	 *
	 * @param mode
	 */
	bool parseGcode(const char* gcode, uint16_t length);

	void printLocation();



};

#endif /* !MOTOR_CONTROLL_H */
