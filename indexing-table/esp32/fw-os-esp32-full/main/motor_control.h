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
#include <atomic>
#include <esp32-hal-gpio.h>
#include <hal/gpio_hal.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <driver/mcpwm.h>
#include <driver/timer.h>

#define RPM_TO_PAUSE(speed, steps_per_revolution, gear_ratio) (60L * 1000L * 1000L / steps_per_revolution / gear_ratio / speed) // ->
#define ANGLE_TO_STEP(angle, steps_per_revolution, gear_ratio) (angle * steps_per_revolution * gear_ratio / 360)
#define STEP_TO_ANGLE(step, steps_per_revolution, gear_ratio) (unit == Unit::STEPS ? step : step * 360 / steps_per_revolution / gear_ratio)
#define ANGLE_DISTANCE(angle1, angle2) (angle1 > angle2 ? angle1 - angle2 : angle2 - angle1)

#define port_TICK_PERIOD_US portTICK_PERIOD_MS * 1000

#define STEPPER1_MCPWM_UNIT MCPWM_UNIT_0
#define STEPPER1_MCPWM_TIMER MCPWM_TIMER_0
#define STEPPER2_MCPWM_UNIT MCPWM_UNIT_0
#define STEPPER2_MCPWM_TIMER MCPWM_TIMER_1

#define STEPPER_COMPLETE_BIT_1 BIT0
#define STEPPER_COMPLETE_BIT_2 BIT1



enum ProgrammingMode : uint8_t {
	NO_PROGRAMM = 0,
	PROGRAMMING = 1,
	RUN_PROGRAM = 2,
};

enum Direction : uint8_t {
	FORWARD = 0,
	BACKWARD = 1,
};

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

typedef struct {
	// variables that can be accessed from multiple tasks
	std::atomic<int32_t> stepsToGo; // number of steps to go

	// thse variables are only accessed from the motorMoveTask
	MotorMode mode = MotorMode::STEPPER; // motor mode
	uint64_t pause = 0;
	uint32_t rpm = 0; // speed of the motor
	uint64_t nextStepTime = 0; // time of the last step
	uint8_t stepNumber = 0; // current step number (used to properly cycle pin states)
	uint16_t angle; // current angle (always in absolute mode)
	int16_t angleMax = -1; // maximum angle
	int16_t angleMin = -1; // minimum angle
} motorVariables;


typedef struct {
    int32_t steps;
    float rpm;
    bool direction; // true = forward, false = backward
    bool complete;
} stepper_command_t;



// TODO move to MotorControl, volatile must be moved to atomic
static volatile uint32_t horiPulseCount = 0;
static volatile uint32_t tiltPulseCount = 0;
static stepper_command_t horiCurrentCommand;
static stepper_command_t tiltCurrentCommand;
static QueueHandle_t horiCommandQueue;
static QueueHandle_t tiltCommandQueue;
static EventGroupHandle_t stepperCompleteEventGroup;


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
		static void IRAM_ATTR timerISRHandleHori(void *arg);
		static void IRAM_ATTR timerISRHandleTilt(void *arg);

		static void horiTask(void *arg);
		static void tiltTask(void *arg);

		void setHoriCommand(int32_t steps, float rpm);
		void setTiltCommand(int32_t steps, float rpm);


		void mcpwmInit();
		void timerInit();


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




		MotorControl();

	public:
		constexpr static char TAG[] = "MotorControl";

		// operational variables of the motors, std::atomic seemed to be fastest, rtos synchronization was slower
		motorVariables horiVariables;
		motorVariables tiltVariables;



		static MotorControl* getInstance()
		{
			if(instance == nullptr)
			{
				instance = new MotorControl();
			}
			return instance;
		};



		uint8_t call(uint16_t id) override;

		/**
		 * @brief parses incoming gcode and prepares its execution
		 *
		 * @param mode
		 */
		bool parseGcode(const char* gcode, uint16_t length);



		/**
		 * @brief stops the horizontal motor, powers down all pins
		 */
		void horiStop();

		/**
		 * @brief stops the tilt motor, powers down all pins
		 */
		void tiltStop();


		void printLocation();

		void home(){
			homeRoutine(0);
		};



};

#endif /* !MOTOR_CONTROLL_H */
