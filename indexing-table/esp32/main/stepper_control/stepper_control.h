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
#include <cmath>
#include <driver/gpio.h>
#include <esp32-hal-gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <functional>
#include <hal/gpio_hal.h>
#include <list>
#include <queue>
#include <stepper_hal.h>
#include <vector>

// #define INT16_MAX 32767
// #define INT16_MIN -32768

#define ANGLE_TO_STEPS(angle, steps_per_revolution) ((angle * steps_per_revolution / 360))
#define STEPS_TO_ANGLE(step, steps_per_revolution) ((float)(step * 360. / steps_per_revolution))
#define ANGLE_DISTANCE(from, to, angleMax) ( \
		(((to) - (from) + (angleMax)) % (angleMax) > (angleMax) / 2) ? ((to) - (from) + (angleMax)) % (angleMax) - (angleMax) : ((to) - (from) + (angleMax)) % (angleMax)) // only applies when no limits are set
#define ANGLE_DISTANCE_COUNTERCLOCKWISE(from, to, angleMax) ((to) - (from) + (angleMax)) % (angleMax) - (angleMax)
#define ANGLE_DISTANCE_CLOCKWISE(from, to, angleMax) ((to) - (from) + (angleMax)) % (angleMax)
#define LIMIT_NUMBER(value, min, max) ((value) < (min) ? (min) : (value) > (max) ? (max) : (value))

#define NORMALIZE_ANGLE(angle, angleMax) ((angle) < 0 ? ((angle) % (angleMax) + (angleMax)) % (angleMax) : (angle) % (angleMax))
#define SYNCHRONIZED (command->movementYaw != nullptr && command->movementPitch != nullptr)

#define port_TICK_PERIOD_US portTICK_PERIOD_MS * 1000

#define PROG_NAME_MAX_LENGTH 24

#define STEPPER1_MCPWM_UNIT MCPWM_UNIT_0
#define STEPPER1_MCPWM_TIMER MCPWM_TIMER_0
#define STEPPER2_MCPWM_UNIT MCPWM_UNIT_0
#define STEPPER2_MCPWM_TIMER MCPWM_TIMER_1

#define STEPPER_COMPLETE_BIT_1 BIT0
#define STEPPER_COMPLETE_BIT_2 BIT1

#define GCODE_ELEMENT_INVALID_FLOAT NAN
#define GCODE_ELEMENT_INVALID_INT 0xFFFFFFFF

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
	INVALID_COMMAND = 1,				 // command wasn't able to be decoded
	INVALID_ARGUMENT = 2,				 // command code is valid but it's arguments aren't
	FAILED_TO_LOCK_QUEUE = 3,		 // command was processed, should be added to noProgrammQueue but we failed to get a lock
	NO_SUPPORT = 4,							 // command exists but isn't yet supported by the hardware
	NOT_PROCESSING_COMMANDS = 5, // we are either running homing or some programm thus new incoming commands will not be process
	CODE_FAILURE = 6,						 // command might be fine but code runned into unexpected occurrence
	NON_CLOSED_LOOP = 7,				 // specific error that can arise only when we are ending programming, indicated that program has unclosed for loop, it is recommended to delete whole program and start again
	COMMAND_BAD_CONTEXT = 8,		 // command is not valid in current context

	RESERVED = 0xFF, // for internal use only
};

// 1st status: 	DONE - commnad is parsed rom string
//							TODO - command parsing is not yet implemented
//
// 2nd status:	DONE - command is implemented in MotorTask
//							TODO - command is not yet implemented in MotorTask
// 							XXX  - command is not handled by MotorTask at all
// 3rd status: 	DONE - command has been validated as working
//						 	TODO - command needs to be validated
enum GCodeCommand : uint8_t {
	//																							1st  2nd  3rd
	M80,	// turn on high voltage supply 						DONE	XXX 	DONE
	M81,	// turn off high voltage supply 					DONE	XXX 	DONE
	G20,	// set units to degrees 									DONE	DONE	DONE
	G21,	// set units to steps 										DONE	DONE	DONE
	G90,	// set absolute positioning 							DONE	DONE	DONE
	G91,	// set relative positioning 							DONE	DONE	DONE
	G92,	// set current position as home  					DONE	DONE	DONE
	G28,	// start homing routine 									DONE	DONE	DONE
	G0,		// move stepper 													DONE	DONE	DONE
	M03,	// start spindle 													DONE	DONE	DONE
	M05,	// stop spindle 													DONE	DONE	DONE
	M92,  // set steps per revolution 						  DONE	DONE	DONE
	M201, // set limits 														DONE	DONE	DONE
	M202, // disable limits 												DONE	DONE	DONE
	P0,		// stop programm execution 								DONE	XXX		DONE
	P1,		// start programm execution 							DONE	XXX		DONE
	P2,		// delete program from memory 						DONE	XXX		DONE
	P90,	// start program declaration (header) 		DONE	XXX		DONE
	P91,	// start program declaration (main body)	DONE	XXX		DONE
	P92,	// end programm declaration 							DONE	XXX		DONE
	P21,	// declare for loop start 								DONE	DONE	DONE
	P22,	// declare for loop end 									DONE	DONE	DONE
	P29,	// declare infinitely looped programm 		DONE	XXX		DONE
	W0,		// wait seconds 													DONE	XXX		DONE
	W1,		// wait milliseconds 											DONE	DONE 	DONE
	W3,   // wait in application layer 							DONE	XXX 	DONE

	// clear stepper QUEUE
	COMMAND_TO_REMOVE
};

// these structures will be stored as a programm declaration
// needs to store
// 	* time in case of wait ()
typedef struct gcode_command_movement_t {
	union {
		int32_t time;				 // for wait
		Direction direction; // for spindle mode
		int16_t steps;			 // for regural steps
		uint32_t iterations; // for for loop
		struct {
			float min;
			float max;
		} limits;
	} val;
	float rpm;

	gcode_command_movement_t()
	{
		// val.time = GCODE_ELEMENT_INVALID_INT;
		val.limits.min = GCODE_ELEMENT_INVALID_INT;
		val.limits.max = GCODE_ELEMENT_INVALID_INT;
		rpm = GCODE_ELEMENT_INVALID_FLOAT;
	};
} gcode_command_movement_t;

typedef struct gcode_command_t {
	GCodeCommand type;
	gcode_command_movement_t* movementYaw = nullptr; // filled in if command requires some action from steppers
	gcode_command_movement_t* movementPitch = nullptr;


	~gcode_command_t()
	{
		if (movementYaw != nullptr)
			delete movementYaw;
		if (movementPitch != nullptr)
			delete movementPitch;
	}
} gcode_command_t;

typedef struct gcode_programm_t {
	char name[PROG_NAME_MAX_LENGTH] = {};

	std::vector<gcode_command_t*>* header = nullptr; // point to a list to save received programms in programming mode
	// iterator to currect header command

	std::vector<gcode_command_t*>* main = nullptr; // point to a list to save received programms in programming mode
	// iterator to currect main command
	uint32_t indexHeader = 0;
	uint32_t indexMain = 0;

	uint32_t indexForLoop = 0;	// index of for loop cycle
	int16_t forLoopCounter = 0; // will decrement on each for loop end, if 0 we wont jump back to for loop start on receiving P93
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
	uint32_t stepsMin = GCODE_ELEMENT_INVALID_INT; // minimum number of steps from home, we can allow multiple rotations possibly
	uint32_t stepsMax = GCODE_ELEMENT_INVALID_INT; // maximum number of steps from home
	int64_t position = 0;
	int32_t positionLastScheduled = 0;
	PositioningMode positioningMode = PositioningMode::RELATIVE;
} stepper_operation_paramters_t;

/*
 * NOTE
 *
 *
 */
class StepperControl {
	private:
	// we will let used create as many programms as they want
	// programm queue must be synchronized as it can be accessed both from parseGcode and motorTask functions
	gcode_programm_t programm;

	// non programm commands will be handled in queue, unfortunately due to need to handle mutext it isn't possible to use totally same access methods anyway so having different ones isn't that big of a problem

	// programms will be stored as list, only used during creation of programms
	std::vector<gcode_command_t*>* commandDestination = nullptr; // point to a list to save received programms in programming mode

	std::list<gcode_programm_t> programms;

	TaskHandle_t commandSchedulerTaskHandle = NULL;

	/**
	 * @brief get new command from queue parses it and schedules it's execution in HAL layer
	 * command is either from noProgrammQueue or activeProgram, than all neccesary calculations
	 * are carried out (limits, absolute positioning) and command is scheduled for execution
	 */
	static void commandSchedulerTask(void* arg);


	/**
	 * callback activated when endstop is triggered
	 * as axis are homed independently we don't need two handlers
	 */
	static void endstopHandler();

	/**
	 * @brief moves stepper to absolute position
	 *
	 * @param stepperHal - HAL struct containing all necessary information to control stepper
	 * @param movement - struct containing information about movement to be executed
	 * @param stepperOpPar - current operational parameters of the stepper
	 * @param synchronized - if true both steppers will wait for each other before executing next command
	 * @return int32_t - new position of the stepper
	 */
	static int32_t moveStepperAbsolute(stepper_hal_struct_t* stepperHal, gcode_command_movement_t* movement, const stepper_operation_paramters_t* stepperOpPar, bool synchronized);

	/**
	 * @brief moves stepper to relative position
	 *
	 * @param stepperHal - HAL struct containing all necessary information to control stepper
	 * @param movement - struct containing information about movement
	 * @param stepperOpPar - current operational parameters of the stepper
	 * @param synchronized - if true both steppers will wait for each other before executing next command
	 * @return int32_t - new position of the stepper
	 */
	static int32_t moveStepperRelative(stepper_hal_struct_t* stepperHal, gcode_command_movement_t* movement, const stepper_operation_paramters_t* stepperOpPar, bool synchronized);

	/**
	 * @brief extract float from string that follows after a given sequence of characters
	 *
	 * @param str - string from which float is extracted
	 * @param length - length of the string
	 * @param startIndex - index from which the search starts
	 * @param matchString - sequence of characters that must be present before the float
	 * @param elementLength - length of the sequence of characters
	 * @return float - extracted float
	 */
	float getElementFloat(const char* str, const uint16_t length, const uint16_t startIndex, const char* matchString, const uint16_t elementLength);

	/**
	 * @brief extract int from string that follows after a given sequence of characters
	 *
	 * @param str - string from which int is extracted
	 * @param length - length of the string
	 * @param startIndex - index from which the search starts
	 * @param matchString - sequence of characters that must be present before the int
	 * @param elementLength - length of the sequence of characters
	 * @return int32_t - extracted int
	 */
	int32_t getElementInt(const char* str, const uint16_t length, const uint16_t startIndex, const char* matchString, const uint16_t elementLength);

	/**
	 * @brief checks if given sequence of characters is present in the string
	 *
	 * @param str - string to be checked
	 * @param length - length of the string
	 * @param startIndex - index from which the search starts
	 * @param matchString - sequence of characters that must be present in the string
	 * @param elementLength - length of the sequence of characters
	 * @return bool - true if the sequence is present, false otherwise
	 */
	bool getElementString(const char* str, const uint16_t length, const uint16_t startIndex, const char* matchString, const uint16_t elementLength);

	/**
	 * @brief parses non scheduled commands
	 *
	 * @param gcode - gcode to be parsed
	 * @param length - length of the gcode
	 * @return ParsingGCodeResult - result of the parsing
	 */
	ParsingGCodeResult parseGCodeNonScheduledCommands(const char* gcode, const uint16_t length);

	/**
	 * @brief parses G commands
	 *
	 * @param gcode - gcode to be parsed
	 * @param length - length of the gcode
	 * @param command - command to be filled with parsed data
	 * @return ParsingGCodeResult - result of the parsing
	 */
	ParsingGCodeResult parseGCodeGCommands(const char* gcode, const uint16_t length, gcode_command_t* command);

	/**
	 * @brief parses M commands
	 *
	 * @param gcode - gcode to be parsed
	 * @param length - length of the gcode
	 * @param command - command to be filled with parsed data
	 * @return ParsingGCodeResult - result of the parsing
	 */
	ParsingGCodeResult parseGCodeMCommands(const char* gcode, const uint16_t length, gcode_command_t* command);

	/**
	 * @brief parses W commands
	 *
	 * @param gcode - gcode to be parsed
	 * @param length - length of the gcode
	 * @param command - command to be filled with parsed data
	 * @return ParsingGCodeResult - result of the parsing
	 */
	ParsingGCodeResult parseGCodeWCommands(const char* gcode, const uint16_t length, gcode_command_t* command);

	/**
	 * @brief parses P commands
	 *
	 * @param gcode - gcode to be parsed
	 * @param length - length of the gcode
	 * @param command - command to be filled with parsed data
	 * @return ParsingGCodeResult - result of the parsing
	 */
	ParsingGCodeResult parseGCodePCommands(const char* gcode, const uint16_t length, gcode_command_t* command);

	/**
	 * @brief starts homing routine for yaw
	 */
	void homeYaw();


	/**
	 * @brief starts homing routine for pitch
	 */
	void homePitch();

	public:
	constexpr static char TAG[] = "StepperControl";

	// operational variables of the steppers, std::atomic seemed to be fastest, rtos synchronization was slower
	inline static std::atomic<ProgrammingMode> programmingMode = ProgrammingMode::NO_PROGRAMM;
	static std::queue<gcode_command_t*> noProgrammQueue; // TODO -> static

	inline static SemaphoreHandle_t noProgrammQueueLock = NULL;

	inline static EventGroupHandle_t homingEventGroup = NULL;
	inline static gcode_programm_t* activeProgram = nullptr; // TODO -> static

	StepperControl();

	/**
	 * @brief parses incoming gcode and prepares its execution
	 *
	 * @param mode
	 */
	ParsingGCodeResult parseGCode(const char* gcode, const uint16_t length);

	void init();

	/**
	 * @brief starts homing routine for both steppers
	 *
	 */
	void home();


};

extern StepperControl stepperControl;

#endif /* !STEPPER_CONTROL_H */
