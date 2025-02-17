/*
 * stepper_hal.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <driver/mcpwm_prelude.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <driver/pulse_cnt.h>
#include <esp_timer.h>
#include <string.h>


#ifndef STEPPER_HAL_H
#define STEPPER_HAL_H

#define STEPPER_COMPLETE_BIT_H BIT0
#define STEPPER_COMPLETE_BIT_T BIT1


// using >> 4 will easily allow us to determine whether the command is synchronized or not
// WARN: Do not mess with the numbering of enums, they are used in the code to save some if statements
enum CommandType : uint8_t {
	STEPPER = 0x01,		// will not wait for second stepper to finish
	SPINDLE = 0x02,								// will spin continuously, PCNT will be turned off (no points in watchdog I think)
	STOP = 0x03,									// will stop the stepper
	SKIP = 0x04,									// will skip the command (used to maintain synchronization between command queues)
	WAIT = 0x05,									// will wait for a given time
};

enum Direction : bool {
	FORWARD = true,
	BACKWARD = false,
};

typedef struct {
	CommandType type;    // type of the command
	union {
		uint32_t steps;			 // number of steps to move to or time to wait in ms
		uint32_t time;       // time to wait in ms
		uint32_t finishTime; // used in one situation: we are moving from spindle command to another command, spindle command is already stored in the previous command register but we need to know how much has the spindle traveled -- thus we store here the endtime.
	} val;
	float rpm;					// speed of the stepper
	bool direction;			// true = forward, false = backward
	bool complete;			// true upon completion of the command, used in correlation with event group
	bool synchronized;	// true if the command is synchronized, if command is stored in prev pointer than used to indicate whether command has been proccessed by the application layer
	uint64_t timestamp; // time of start of the execution
} stepper_hal_command_t;

// these declaration unfortunately have to be here as esp compiler has problems with them being in a class

typedef struct {
		stepper_hal_command_t* stepperCommand;
		stepper_hal_command_t* stepperCommandPrev;
		QueueHandle_t commandQueue = NULL;

		mcpwm_timer_handle_t timer = NULL;

		mcpwm_oper_handle_t oper = NULL;
		mcpwm_cmpr_handle_t comparator = NULL;
		mcpwm_gen_handle_t generator = NULL;

		// PCNT variables
		pcnt_unit_handle_t pcntUnit = NULL;
		pcnt_channel_handle_t pcntChan = NULL;
		QueueHandle_t pcntQueue;
		uint8_t stepperCompleteBit = 0;
		gpio_num_t stepperDirectionPin = (gpio_num_t) 0;


} stepper_hal_struct_t;



/**
 * @brief class that handles the stepper control
 * essentially only a container for functions that rescript access to them
 */
class StepperHal{

	public:
		static bool pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

		constexpr static  char TAG[] = "StepperHal";


		static void stepperTask(void *arg);


		inline static EventGroupHandle_t stepperEventGroup = NULL;

		/**
		 * @brief initializes the PWM generator
		 */
		void initMCPWN();

		/**
		 * @brief initializes the PCNT
		 */
		void initPCNT();


		/**
		 * @brief initializes the tasks that will handle the steppers
		 */
		void initStepperTasks();


		/**
		 * @brief get number of commands queued for stepper T
		 *
		 * @param uint8_t - number of commands in queue
		 */
		uint8_t getQueueLength(stepper_hal_struct_t* stepperHal);

		/**
		 * @brief peek at next command in queue for stepper H
		 *
		 * @param pointer - stepper_hal_command_t* to which next command will be stored
		 */
		bool peekQueue(stepper_hal_struct_t* stepperHal, stepper_hal_command_t* pointer);



		/**
		 * @brief steps stepper H a given number of steps
		 * if steps is set to zero command will be interpreted as CommandType::SKIP
		 * this can be used to maintain synchronization between command queues of two steppers
		 *
		 * @param steps  - number of steps stepper will take, limited to +-32767
		 * @param rmp - rotations per minute
		 * @param synchronized - if true stepper H will wait for stepper T to finish before moving to next task
		 * @return true if command was added successfully
		 */
		bool stepStepper(stepper_hal_struct_t* stepperHal, int16_t steps, float rpm, bool synchronized = false);

		// /**
		//  * @brief steps stepper T a given number of steps
		//  * if steps is set to zero command will be interpreted as CommandType::SKIP
		//  * this can be used to maintain synchronization between command queues of two steppers
		//  *
		//  * @param steps  - number of steps stepper will take, limited to +-32767
		//  * @param rmp - rotations per minute
		//  * @param synchronized - if true stepper T will wait for stepper H to finish before moving to next task
		//  * @return true if command was added successfully
		//  */
		// bool stepStepperT(int16_t steps, float rpm, bool synchronized = false);

		/**
		 *	@brief waits for a given time on stepper H
		 *
		 * @param time - time in ms to wait
		 * @param synchronized - if true stepper H will wait for stepper T to finish before moving to next task
		 * @return true if command was added successfully
		 */
		bool waitStepper(stepper_hal_struct_t* stepperHal, uint32_t time, bool synchronized = false);



		/**
		 * @brief activates spindle on stepper T
		 * spindle is never synchronized
		 *
		 * @param rpm - rotations per minute
		 * @return true if command was added successfully
		 */
		bool spindleStepper(stepper_hal_struct_t* stepperHal, float rpm, Direction direction);


		/**
		 * @brief stops the stepper T
		 *
		 * @return true if command was added successfully
		 */
		bool stopStepper(stepper_hal_struct_t* stepperHal, bool synchronized = false);


		/**
		 * @brief immediately stops the stepper T, clears the queue
		 */
		void stopNowStepper(stepper_hal_struct_t* stepperHal);


		/**
		 * @brief skip command, used to maintain synchronization between command queues
		 */
		bool skipStepper(stepper_hal_struct_t* stepperHal, bool synchronized = true);

		/**
		 * @brief clears command queue and issues stop command
		 * use of this function should not lead to devalidation of current position
		 *
		 * @return true if queue was cleared successfully
		 */
		bool carefullStop();

		/**
		 * @brief clears command queue for stepper H
		 *
		 * @return true if queue was cleared successfully
		 */
		bool clearQueue(stepper_hal_struct_t* stepperHal);



		/**
		 * @brief return number of steps traveled by stepper T since start of the command
		 *
		 * @return int64_t - number of steps
		 */
		int64_t getStepsTraveledOfCurrentCommand(stepper_hal_struct_t* stepperHal);



		/**
		 * @brief return number of steps traveled by stepper T in previous command
		 * if synchronized of the previous command is set true than the number of steps will be zero
		 *
		 * @return int64_t - number of steps
		 */
		int64_t getStepsTraveledOfPrevCommand(stepper_hal_struct_t* stepperHal);



};


extern StepperHal steppers;
extern stepper_hal_struct_t* stepperHalH;
extern stepper_hal_struct_t* stepperHalT;


#endif /* !STEPPER_HAL_H */
