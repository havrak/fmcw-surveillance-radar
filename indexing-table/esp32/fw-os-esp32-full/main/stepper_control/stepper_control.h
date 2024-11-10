/*
 * stepper_control.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 *
 * NOTE: in oder not to waste any time in cycle controlling the stepper steppers so that other
 * tasks can run in the meantime code here is rather wasteful. If you don't have any requirements
 * on perforamance use PerStepperDriver class from peripheral library
 */

#ifndef STEPPER_CONTROL_H
#define STEPPER_CONTROL_H

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
#include <stepper_hal.h>
#include <queue>

#define HOMING_DONE_BIT BIT2

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

#define GCODE_ELEMENT_INVALID_FLOAT NAN
#define GCODE_ELEMENT_INVALID_INT 0xFFFFFFFFFFFFFFFF



enum ProgrammingMode : uint8_t {
	NO_PROGRAMM = 0, // commands are executed as they come in
	HOMING =      1, // special mode used when device is homing, all commands will be dumped
	PROGRAMMING = 2, // all programs are stashed and saved to programm schema
	RUN_PROGRAM = 3, // we are running a program
};

enum PositioningMode : uint8_t {
	ABSOLUTE = 2,
	RELATIVE = 3,
};

enum Unit : uint8_t {
	DEGREES = 0,
	STEPS = 1,
};

enum ParsingGCodeResult: uint8_t {
	SUCCESS = 0,              		// processing was successful
	INVALID_COMMAND = 2,       		// command wasn't able to be decoded
	INVALID_ARGUMENT = 3,					// command code is valid but it's arguments aren't
	FAILED_TO_LOCK_QUEUE = 4, 		// command was processed, should be added to noProgrammQueue but we failed to get a lock
	NO_SUPPORT = 5,           		// command exists but isn't yet supported by the hardware
	NOT_PROCESSING_COMMANDS = 6,  // we are either running homing or some programm thus new incoming commands will not be process
	CODE_FAILURE = 7, 						// command might be fine but code runned into unexpected occurrence
	NON_CLOSED_LOOP = 8, 					// specific error that can arise only when we are ending programming, indicated that program has unclosed for loop, it is recommended to delete whole program and start again
};

enum GCodeCommand : uint8_t {
	M80, // turn on high voltage supply TODO
	M81, // turn off high voltage supply TODO
	G20, // set units to degrees DONE
	G21, // set units to steps DONE
	G90, // set absolute positioning DONE
	G91, // set relative positioning DONE
	G92, // set current position as home DONE
	G28, // move to home from current position DONE
	G0,  // move stepper DONE
	M03, // start spindle DONE
	M05, // stop spindle DONE NOTE: should be issued only when stepper is in spindle mode
	P0,  // stop programm execution DONE
	P1,  // start programm execution
	P2,  // delete program from memory
	P90, // start program declaration (header) DONE
	P91, // start program declaration (main body) DONE
	P92, // end programm declaration DONE
	P21, // declare for loop start DONE
	P22, // declare for loop end DONE
	P29, // declare infinitely looped programm DONE
	W0,  // wait seconds DONE
	W1,  // wait milliseconds DONE
};

// these structures will be stored as a programm declaration
// needs to store
// 	* time in case of wait ()
typedef struct {
	union{
		uint64_t time; // for wait
		Direction direction; // for spindle mode
		int16_t steps; // for regural steps
	} val;
	float rpm;
} gcode_command_movement_t;

typedef struct gcode_command_t{
	GCodeCommand  type;
	gcode_command_movement_t* movementH = nullptr; // filled in if command requires some action from steppers
	gcode_command_movement_t* movementT = nullptr;

	~gcode_command_t(){
		delete movementH;
		delete movementT;
	}
} gcode_command_t;


typedef struct gcode_programm_t{
		char name[20];

		std::list<gcode_command_t>* header = nullptr; // point to a list to save received programms in programming mode
		// iterator to currect header command
		std::list<gcode_command_t>::iterator headerIterator;
		std::list<gcode_command_t>* main = nullptr; // point to a list to save received programms in programming mode
		// iterator to currect main command
		std::list<gcode_command_t>::iterator mainIterator;


		// single for loop cycle
		std::list<gcode_command_t>::iterator forLoop; // point to a start of for loop cycle
		int16_t forLoopCounter = 0; 	// will decrement on each for loop end, if 0 we wont jump back to for loop start on receiving P93
																	// NOTE: remember to reset if we are repeating indefinitely

		bool repeatIndefinitely = false; // main body will be repeated indefinitely

		gcode_programm_t(){
			header = new std::list<gcode_command_t>();
			main = new std::list<gcode_command_t>();
		}

		void clean(){
			header->clear();
			main->clear();
			repeatIndefinitely = false;
			forLoopCounter = 0;
		}

		void reset(){
			headerIterator = header->begin();
			mainIterator = main->begin();
			forLoopCounter = 0;
		}
} gcode_programm_t;


typedef struct {
	std::atomic<int32_t> stepNumber = 0; // current step number
	int16_t angleMax = -1; // maximum angle in steps (+ form home)
	int16_t angleMin = -1; // minimum angle in steps (- from home)
} stepper_variables_t;



/*
 * NOTE
 *
 *
 */
class StepperControl : public CallbackInterface{
	private:



		// we will let used create as many programms as they want
		// programm queue must be synchronized as it can be accessed both from parseGcode and motorTask functions
		gcode_programm_t programm;

		// non programm commands will be handled in queue, unfortunately due to need to handle mutext it isn't possible to use totally same access methods anyway so having different ones isn't that big of a problem


		// programms will be stored as list, only used during creation of programms
		std::list<gcode_command_t>* commandDestination = nullptr; // point to a list to save received programms in programming mode

		SemaphoreHandle_t noProgrammQueueLock;
		std::queue<gcode_command_t> noProgrammQueue; // TODO -> static
		gcode_programm_t* activeProgram = nullptr;   // TODO -> static
		std::list<gcode_programm_t> programms;






		TaskHandle_t stepperMoveTaskHandle = NULL;

		static void stepperMoveTask(void *arg);
		static void tiltEndstopHandler(void *arg);
		static void horizontalEndstopHandler(void *arg);

		static void horiTask(void *arg);
		static void tiltTask(void *arg);

		void setHoriCommand(int32_t steps, float rpm);
		void setTiltCommand(int32_t steps, float rpm);


		void mcpwmInit();
		void timerInit();




		/**
		 * @brief cycles trough GPIO states to move the stepper
		 * not to be used outside functions handeling stepping
		 *
		 * NOTE: copy of tiltSwitchOutput functions just macros resolve to different pins
		 *
		 * @param step
		 */
		void horiSwitchOutput(uint8_t step);


		/**
		 * @brief cycles trough GPIO states to move the stepper
		 * not to be used outside functions handeling stepping
		 *
		 * NOTE: copy of horiSwitchOutput functions just macros resolve to different pins
		 *
		 * @param step
		 */
		void tiltSwitchOutput(uint8_t step);





	public:
		constexpr static char TAG[] = "StepperControl";

		// operational variables of the steppers, std::atomic seemed to be fastest, rtos synchronization was slower
		inline static EventGroupHandle_t homingEventGroup = NULL;
		inline static ProgrammingMode programmingMode = ProgrammingMode::NO_PROGRAMM;
		inline static PositioningMode positioningMode = PositioningMode::ABSOLUTE;

		inline static Unit unit = Unit::DEGREES;

		inline static stepper_variables_t varsH;
		inline static stepper_variables_t varsT;


		StepperControl();



		uint8_t call(uint16_t id) override;

		/**
		 * @brief parses incoming gcode and prepares its execution
		 *
		 * @param mode
		 */
		ParsingGCodeResult parseGCode(const char* gcode, uint16_t length);

		void init();



		/**
		 * @brief stops the horizontal stepper, powers down all pins
		 */
		void horiStop();

		/**
		 * @brief stops the tilt stepper, powers down all pins
		 */
		void tiltStop();


		void printLocation();

		void home();




};

extern StepperControl stepperControl;

#endif /* !STEPPER_CONTROL_H */
