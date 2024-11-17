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

#include <atomic>
#include <callback_interface.h>
#include <driver/gpio.h>
#include <esp32-hal-gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <hal/gpio_hal.h>
#include <os_core_tasker_ids.h>
#include <queue>
#include <vector>
#include <stepper_hal.h>
#include <tasker_singleton_wrapper.h>
#include <functional>

#define HOMING_DONE_BIT BIT2

#define ANGLE_TO_STEPS(angle, steps_per_revolution) (angle * steps_per_revolution / 360)
#define STEPS_TO_ANGLE(step, steps_per_revolution) (step * 360. / steps_per_revolution)
#define ANGLE_DISTANCE(from, to, angleMax) ( \
    (((to) - (from) + (angleMax)) % (angleMax) > (angleMax) / 2) ? \
    ((to) - (from) + (angleMax)) % (angleMax) - (angleMax) : \
    ((to) - (from) + (angleMax)) % (angleMax) \
		) // only applies when no limits are set
#define ANGLE_DISTANCE_COUNTERCLOCKWISE(from, to, angleMax) ((to) - (from) + (angleMax)) % (angleMax) - (angleMax)
#define ANGLE_DISTANCE_CLOCKWISE(from, to, angleMax) ((to) - (from) + (angleMax)) % (angleMax)

#define NORMALIZE_ANGLE(angle, angleMax) ((angle) < 0 ? ((angle) % (angleMax) + (angleMax)) % (angleMax) : (angle) % (angleMax))
#define SYNCHRONIZED  (command->movementH != nullptr && command->movementT != nullptr)

#define port_TICK_PERIOD_US portTICK_PERIOD_MS * 1000

#define STEPPER1_MCPWM_UNIT MCPWM_UNIT_0
#define STEPPER1_MCPWM_TIMER MCPWM_TIMER_0
#define STEPPER2_MCPWM_UNIT MCPWM_UNIT_0
#define STEPPER2_MCPWM_TIMER MCPWM_TIMER_1

#define STEPPER_COMPLETE_BIT_1 BIT0
#define STEPPER_COMPLETE_BIT_2 BIT1

#define GCODE_ELEMENT_INVALID_FLOAT NAN
#define GCODE_ELEMENT_INVALID_INT   0xFFFFFFFFFFFFFFFF
#define GCODE_ELEMENT_INVALID_INT32 0xFFFFFFFF

enum ProgrammingMode : uint8_t {
	NO_PROGRAMM = 0, // commands are executed as they come in
	HOMING = 1,			 // special mode used when device is homing, all commands will be dumped
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

enum ParsingGCodeResult : uint8_t {
	SUCCESS = 0,								 // processing was successful
	INVALID_COMMAND = 2,				 // command wasn't able to be decoded
	INVALID_ARGUMENT = 3,				 // command code is valid but it's arguments aren't
	FAILED_TO_LOCK_QUEUE = 4,		 // command was processed, should be added to noProgrammQueue but we failed to get a lock
	NO_SUPPORT = 5,							 // command exists but isn't yet supported by the hardware
	NOT_PROCESSING_COMMANDS = 6, // we are either running homing or some programm thus new incoming commands will not be process
	CODE_FAILURE = 7,						 // command might be fine but code runned into unexpected occurrence
	NON_CLOSED_LOOP = 8,				 // specific error that can arise only when we are ending programming, indicated that program has unclosed for loop, it is recommended to delete whole program and start again
	COMMAND_BAD_CONTEXT = 9, 		 // command is not valid in current context
};

// 1st status: 	DONE - commnad is parsed rom string
//							TODO - command parsing is not yet implemented
//
// 2nd status:	DONE - command is implemented in MotorTask
//							TODO - command is not yet implemented in MotorTask
// 							XXX  - command is not handled by MotorTask at all
enum GCodeCommand : uint8_t {
	M80, // turn on high voltage supply DONE XXX
	M81, // turn off high voltage supply TODO XXX
	G20, // set units to degrees DONE DONE
	G21, // set units to steps DONE DONE
	G90, // set absolute positioning DONE DONE
	G91, // set relative positioning DONE DONE
	G92, // set current position as home  DONE DONE
	G28, // start homing routine DONE DONE
	G0,	 // move stepper DONE TODO
	M03, // start spindle DONE DONE
	M05, // stop spindle DONE DONE
	M201, // set limits DONE DONE
	M202, // disable limits DONE DONE
	P0,	 // stop programm execution DONE XXX
	P1,	 // start programm execution DONE XXX?????
	P2,	 // delete program from memory DONE XXX????? - could be runned in motorTask
	P90, // start program declaration (header) DONE XXX
	P91, // start program declaration (main body) DONE XXX
	P92, // end programm declaration DONE XXX
	P21, // declare for loop start DONE DONE
	P22, // declare for loop end DONE DONE
	P29, // declare infinitely looped programm DONE XXX
	W0,	 // wait seconds DONE XXX
	W1,	 // wait milliseconds DONE


	// clear stepper QUEUE
};

// these structures will be stored as a programm declaration
// needs to store
// 	* time in case of wait ()
typedef struct gcode_command_movement_t{
	union {
		uint64_t time;			 // for wait
		Direction direction; // for spindle mode
		int16_t steps;			 // for regural steps
		uint32_t iterations;	 // for for loop
		struct {
			int32_t min;
			int32_t max;
		} limits;
	} val;
	float rpm;

	gcode_command_movement_t(){
		val.time = GCODE_ELEMENT_INVALID_INT;
		rpm = GCODE_ELEMENT_INVALID_FLOAT;
	};
} gcode_command_movement_t;

typedef struct gcode_command_t {
	GCodeCommand type;
	gcode_command_movement_t* movementH = nullptr; // filled in if command requires some action from steppers
	gcode_command_movement_t* movementT = nullptr;

	~gcode_command_t()
	{
		delete movementH;
		delete movementT;
	}
} gcode_command_t;

typedef struct gcode_programm_t {
	char name[20];

	std::vector<gcode_command_t*>* header = nullptr; // point to a list to save received programms in programming mode
	// iterator to currect header command

	std::vector<gcode_command_t*>* main = nullptr; // point to a list to save received programms in programming mode
	// iterator to currect main command
	uint32_t indexHeader = 0;
	uint32_t indexMain = 0;

	uint32_t indexForLoop = 0; // index of for loop cycle
	int16_t forLoopCounter = 0;										// will decrement on each for loop end, if 0 we wont jump back to for loop start on receiving P93
																								// NOTE: remember to reset if we are repeating indefinitely

	bool repeatIndefinitely = false; // main body will be repeated indefinitely

	gcode_programm_t()
	{
		header = new std::vector<gcode_command_t*>();
		main = new std::vector<gcode_command_t*>();
	}

	void clean()
	{
		header->clear();
		main->clear();
		repeatIndefinitely = false;
		forLoopCounter = 0;
	}

	void reset()
	{
		indexHeader = 0;
		indexMain = 0;
		forLoopCounter = 0;
	}
} gcode_programm_t;

typedef struct {
	int32_t stepsMin = GCODE_ELEMENT_INVALID_INT32;							 // minimum number of steps from home, we can allow multiple rotations possibly
	int32_t stepsMax = GCODE_ELEMENT_INVALID_INT32;							 // maximum number of steps from home
	int32_t position = 0;
	int32_t positionLastScheduled = 0;
	PositioningMode positioningMode = PositioningMode::RELATIVE;
} stepper_variables_t;

/*
 * NOTE
 *
 *
 */
class StepperControl : public CallbackInterface {
	private:
	// we will let used create as many programms as they want
	// programm queue must be synchronized as it can be accessed both from parseGcode and motorTask functions
	gcode_programm_t programm;

	// non programm commands will be handled in queue, unfortunately due to need to handle mutext it isn't possible to use totally same access methods anyway so having different ones isn't that big of a problem

	// programms will be stored as list, only used during creation of programms
	std::vector<gcode_command_t*>* commandDestination = nullptr; // point to a list to save received programms in programming mode

	std::list<gcode_programm_t> programms;

	TaskHandle_t stepperMoveTaskHandle = NULL;

	static void stepperMoveTask(void* arg);
	static void tiltEndstopHandler(void* arg);
	static void horizontalEndstopHandler(void* arg);

	static void horiTask(void* arg);
	static void tiltTask(void* arg);

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
	inline static std::atomic<ProgrammingMode> programmingMode = ProgrammingMode::NO_PROGRAMM;
	static SemaphoreHandle_t noProgrammQueueLock;
	static std::queue<gcode_command_t*> noProgrammQueue; // TODO -> static

	inline static EventGroupHandle_t homingEventGroup = NULL;
	inline static gcode_programm_t* activeProgram = nullptr;	 // TODO -> static


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
