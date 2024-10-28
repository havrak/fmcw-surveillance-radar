/*
 * motor_control.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 *
 * NOTE: in oder not to waste any time in cycle controlling the stepper motors so that other
 * tasks can run in the meantime code here is rather wasteful. If you don't have any requirements
 * on perforamance use PerStepperDriver class from peripheral library
 */

#ifndef MOTOR_CONTROLL_H
#define MOTOR_CONTROLL_H

#include <esp_log.h>
#include <callback_interface.h>
#include <os_core_tasker_ids.h>
#include <tasker_singleton_wrapper.h>


#define RPM_TO_PAUSE(speed, steps_per_revolution, gear_ratio) (60L * 1000L * 1000L / steps_per_revolution / gear_ratio / speed) // ->
#define ANGLE_TO_STEP(angle, steps_per_revolution, gear_ratio) (angle * steps_per_revolution * gear_ratio / 360)
#define STEP_TO_ANGLE(step, steps_per_revolution, gear_ratio) (unit == Unit::STEPS ? step : step * 360 / steps_per_revolution / gear_ratio)
#define ANGLE_DISTANCE(angle1, angle2) (angle1 > angle2 ? angle1 - angle2 : angle2 - angle1)

#define port_TICK_PERIOD_US portTICK_PERIOD_MS * 1000

enum PositioningMode : uint8_t {
	HOMING_FAST = 0,
	HOMING_SLOW = 1,
	ABSOLUTE = 2,
	RELATIVE = 3,
};

enum Unit : uint8_t {
	DEGREES = 0,
	STEPS = 1,
};

enum MotorMode : uint8_t {
	HOMING = 0,
	STOPPED = 1,
	SPINDLE_CLOCKWISE = 2,
	SPINDLE_COUNTERCLOCKWISE = 3,
	STEPPER = 4,
};


// constant/not frequently changing motor configuration, doesn't need to be volatile
// NOTE: pin numbers for stepper motors are hardcoded with Kconfig values, as opposed to the rest of the configuration there should be no need to change them ever
typedef struct {
	int8_t endstop = -1; // endstop pin
	int16_t angleMax = -1; // maximum angle
	int16_t angleMin = -1; // minimum angle
	uint16_t gearRatio = 1; // gear ratio (only down gearing)
	uint16_t stepCount = 100; // steps per revolution

	uint8_t endstop; // endstop pin
	GPIORegime endstopRegime; // endstop regime
}  __attribute__((packed)) motorConfiguration;

// motor variables that can be accessed frequently from multiple tasks, thus atomic operations are needed
typedef struct {
	MotorMode mode = MotorMode::STEPPER; // motor mode
	uint16_t angle; // current angle (always in absolute mode)
	int32_t stepsToGo; // number of steps to go
	uint32_t pause = 1000'0000; // default sleep roughly ones pers second
	uint64_t lastStepTime = 0; // time of the last step
	uint8_t stepNumber = 0; // current step number (used to properly cycle pin states)
}  __attribute__((packed)) motorVariables;


/*
 * NOTE
 *
 *
 */
class MotorControl : public CallbackInterface{
	private:
		static MotorControl* instance;

		PositioningMode positioningMode = PositioningMode::ABSOLUTE;

		Unit unit = Unit::DEGREES;




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


		/**
		 * @brief cycles trough GPIO states to move the motor
		 * not to be used outside functions handeling stepping
		 *
		 * NOTE: copy of tiltSwitchOutput functions just macros resolve to different pins
		 *
		 * @param step
		 */
		void horiSwitchOutput(uint8_t step);


		/**
		 * @brief cycles trough GPIO states to move the motor
		 * not to be used outside functions handeling stepping
		 *
		 * NOTE: copy of horiSwitchOutput functions just macros resolve to different pins
		 *
		 * @param step
		 */
		void tiltSwitchOutput(uint8_t step);


		void tiltStep(int32_t steps);

		void horiStep(int32_t steps);



		MotorControl();

	public:
		constexpr static char TAG[] = "MotorControl";


		// configuration of the motors
		motorConfiguration horiConfiguration;
		motorConfiguration tiltConfiguration;


		// operational variables of the motors, std::atomic seemed to be fastest, rtos synchoronization was slower
		std::atomic<motorVariables> horiVariables;
		std::atomic<motorVariables> tiltVariables;



		static MotorControl* getInstance()
		{
			if(instance == nullptr)
			{
				instance = new MotorControl();
			}
			return instance;
		};




#ifdef CONFIG_MOTR_4WIRE_MODE

#else

		/**
		 * @brief Set the Horizontal motor parametrs
		 * For step count multiply the number of steps with your microstepping configuration
		 *
		 * @param stepCount steps per revolution of the horizontal motor
		 * @param gearRatio gear ratio of the horizontal motor
		 */
		void setHorizontalMotor(uint8_t stepCount = 200, uint8_t dirPin, uint8_t stepPin, uint8_t gearRatio =1 );

		void setTiltMotor(uint8_t stepCount = 200, uint8_t dirPin, uint8_t stepPin, uint8_t gearRatio = 1);
#endif /* CONFIG_MOTR_4WIRE_MODE */



		void setEndstops(int8_t horizontal = -1, int8_t tilt = -1);

		uint8_t call(uint16_t id) override;

		/**
		 * @brief parses incoming gcode and prepares its execution
		 *
		 * @param mode
		 */
		bool parseGcode(const char* gcode, uint16_t length);


		void printLocation();

		void home(){
			homeRoutine(0);
		};



};

#endif /* !MOTOR_CONTROLL_H */
