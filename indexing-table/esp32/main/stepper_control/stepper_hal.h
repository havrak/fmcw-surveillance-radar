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


} stepper_hal_variables_t;


/**
 * @brief class that handles the stepper control
 * essentially only a container for functions that rescript access to them
 */
class StepperHal{

	public:
		static bool pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

		constexpr static  char TAG[] = "StepperHal";


		static void stepperTask(void *arg);

		static stepper_hal_variables_t varsHalH;
		static stepper_hal_variables_t varsHalT;

		/**
		 * previous command issued, used by the application layer to determine
		 * what number of steps last command had so we can calculate the current angle
		 * timestmap will be time of their finish
		 */
		// inline static stepper_hal_command_t* varsHalH.stepperCommand;
		// inline static stepper_hal_command_t* varsHalT.stepperCommand;
		// inline static stepper_hal_command_t* stepperCommandPrevH;
		// inline static stepper_hal_command_t* stepperCommandPrevT;
		// inline static QueueHandle_t varsHalH.commandQueue = NULL;
		// inline static QueueHandle_t varsHalT.commandQueue = NULL;
		inline static EventGroupHandle_t stepperEventGroup = NULL;
		//
		// inline static mcpwm_timer_handle_t timerH = NULL;
		// inline static mcpwm_timer_handle_t timerT = NULL;
		//
		// inline static mcpwm_oper_handle_t operatorH = NULL;
		// inline static mcpwm_oper_handle_t operatorT = NULL;
		// inline static mcpwm_cmpr_handle_t comparatorH = NULL;
		// inline static mcpwm_cmpr_handle_t comparatorT = NULL;
		// inline static mcpwm_gen_handle_t generatorH = NULL;
		// inline static mcpwm_gen_handle_t generatorT = NULL;
		//
		// // PCNT variables
		// inline static pcnt_unit_handle_t varsHalH.pcntUnit = NULL;
		// inline static pcnt_unit_handle_t varsHalT.pcntUnit = NULL;
		// inline static pcnt_channel_handle_t pcntChanH = NULL;
		// inline static pcnt_channel_handle_t pcntChanT = NULL;
		// inline static QueueHandle_t pcntQueueH;
		// inline static QueueHandle_t pcntQueueT;
		//

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
		 * @brief get number of commands queued for stepper H
		 *
		 * @param uint8_t - number of commands in queue
		 */
		uint8_t getQueueLengthH(){
			return uxQueueMessagesWaiting(varsHalH.commandQueue);
		}

		/**
		 * @brief get number of commands queued for stepper T
		 *
		 * @param uint8_t - number of commands in queue
		 */
		uint8_t getQueueLengthT(){
			return uxQueueMessagesWaiting(varsHalT.commandQueue);
		}

		/**
		 * @brief peek at next command in queue for stepper H
		 *
		 * @param pointer - stepper_hal_command_t* to which next command will be stored
		 */
		bool peekQueueH(stepper_hal_command_t* pointer){
			return xQueuePeek(varsHalH.commandQueue, pointer, portMAX_DELAY) == pdTRUE;
		}

		/**
		 * @brief peek at next command in queue for stepper T
		 *
		 * @param pointer - stepper_hal_command_t* to which next command will be stored
		 */
		bool peekQueueT(stepper_hal_command_t* pointer){
			return xQueuePeek(varsHalT.commandQueue, pointer, portMAX_DELAY) == pdTRUE;
		}


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
		bool stepStepperH(int16_t steps, float rpm, bool synchronized = false);

		/**
		 * @brief steps stepper T a given number of steps
		 * if steps is set to zero command will be interpreted as CommandType::SKIP
		 * this can be used to maintain synchronization between command queues of two steppers
		 *
		 * @param steps  - number of steps stepper will take, limited to +-32767
		 * @param rmp - rotations per minute
		 * @param synchronized - if true stepper T will wait for stepper H to finish before moving to next task
		 * @return true if command was added successfully
		 */
		bool stepStepperT(int16_t steps, float rpm, bool synchronized = false);

		/**
		 *	@brief waits for a given time on stepper H
		 *
		 * @param time - time in ms to wait
		 * @param synchronized - if true stepper H will wait for stepper T to finish before moving to next task
		 * @return true if command was added successfully
		 */
		bool waitStepperH(uint32_t time, bool synchronized = false);

		/**
		 *	@brief waits for a given time on stepper T
		 *
		 * @param time - time in ms to wait
		 * @param synchronized - if true stepper T will wait for stepper H to finish before moving to next task
		 * @return true if command was added successfully
		 */
		bool waitStepperT(uint32_t time, bool synchronized = false);

		/**
		 * @brief activates spindle on stepper
		 * spindle is never synchronized
		 *
		 * @param rpm - rotations per minute
		 * @return true if command was added successfully
		 */
		bool spindleStepperH(float rpm, Direction direction);

		/**
		 * @brief activates spindle on stepper T
		 * spindle is never synchronized
		 *
		 * @param rpm - rotations per minute
		 * @return true if command was added successfully
		 */
		bool spindleStepperT(float rpm, Direction direction);

		/**
		 * @brief stops the stepper H
		 *
		 * @return true if command was added successfully
		 */
		bool stopStepperH(bool synchronized = false);

		/**
		 * @brief stops the stepper T
		 *
		 * @return true if command was added successfully
		 */
		bool stopStepperT(bool synchronized = false);

		/**
		 * @brief immediately stops the stepper H, clears the queue
		 */
		void stopNowStepperH();

		/**
		 * @brief immediately stops the stepper T, clears the queue
		 */
		void stopNowStepperT();

		/**
		 * @brief skip command, used to maintain synchronization between command queues
		 */
		bool skipStepperH(bool synchronized = true);

		/**
		 * @brief skip command, used to maintain synchronization between command queues
		 */
		bool skipStepperT(bool synchronized = true);

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
		bool clearQueueH();

		/**
		 * @brief clears command queue for stepper T
		 *
		 * @return true if queue was cleared successfully
		 */
		bool clearQueueT();

		/**
		 * @brief return number of steps traveled by stepper H since start of the command
		 *
		 * @return int64_t - number of steps
		 */
		int64_t getStepsTraveledOfCurrentCommandH();


		/**
		 * @brief return number of steps traveled by stepper T since start of the command
		 *
		 * @return int64_t - number of steps
		 */
		int64_t getStepsTraveledOfCurrentCommandT();


		/**
		 * @brief return number of steps traveled by stepper H in previous command
		 * if synchronized of the previous command is set true than the number of steps will be zero
		 *
		 * @return int64_t - number of steps
		 */
		int64_t getStepsTraveledOfPrevCommandH();


		/**
		 * @brief return number of steps traveled by stepper T in previous command
		 * if synchronized of the previous command is set true than the number of steps will be zero
		 *
		 * @return int64_t - number of steps
		 */
		int64_t getStepsTraveledOfPrevCommandT();



};

extern StepperHal steppers;


#endif /* !STEPPER_HAL_H */
