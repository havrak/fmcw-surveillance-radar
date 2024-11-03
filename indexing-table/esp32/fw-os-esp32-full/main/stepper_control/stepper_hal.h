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


#ifndef STEPPER_HAL_H
#define STEPPER_HAL_H

#define STEPPER_COMPLETE_BIT_H BIT0
#define STEPPER_COMPLETE_BIT_T BIT1


// using >> 4 will easily allow us to determine whether the command is synchronized or not
// WARN: Do not mess with the numbering of enums, they are used in the code to save some if statements
enum CommandType : uint8_t {
	STEPPER_INDIVIDUAL = 0x01, 		// will not wait for second stepper to finish
	STEPPER_SYNCHRONIZED = 0x12, 	// will wait for both steppers to finish
	SPINDLE = 0x03, 							// will spin continuously, PCNT will be turned off (no points in watchdog I think)
	SPINDLE_SYNCHRONIZED = 0x14, 	// will spin continuously, PCNT will be turned off (no points in watchdog I think)
	STOP = 0x05, 									// will stop the stepper
	STOP_SYNCHRONIZED = 0x15, 		// will stop the stepper
	SKIP = 0x06, 									// will skip the command (used to maintain synchronization between command queues)
	WAIT = 0x07, 									// will wait for a given time
	WAIT_SYNCHRONIZED = 0x17, 		// will wait for a given time
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
	uint64_t timestamp; // time of start of the execution
} stepper_command_t;

// these declaration unfortunately have to be here as esp compiler has problems with them being in a class

// MCPWM variables



/**
 * @brief class that handles the stepper control
 * essentially only a container for functions that rescript access to them
 */
class StepperHal{

	public:
		static bool pcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

		static void stepperTaskH(void *arg);
		static void stepperTaskT(void *arg);

		inline static stepper_command_t* stepperCommandH;
		inline static stepper_command_t* stepperCommandT;

		/**
		 * previous command issued, used by the application layer to determine
		 * what number of steps last command had so we can calculate the current angle
		 * timestmap will be time of their finish
		 */
		inline static stepper_command_t* stepperCommandPrevH;
		inline static stepper_command_t* stepperCommandPrevT;
		inline static QueueHandle_t commandQueueH = NULL;
		inline static QueueHandle_t commandQueueT = NULL;
		inline static EventGroupHandle_t stepperEventGroup = NULL;

		inline static mcpwm_timer_handle_t timerH = NULL;
		inline static mcpwm_timer_handle_t timerT = NULL;

		inline static mcpwm_oper_handle_t operatorH = NULL;
		inline static mcpwm_oper_handle_t operatorT = NULL;
		inline static mcpwm_cmpr_handle_t comparatorH = NULL;
		inline static mcpwm_cmpr_handle_t comparatorT = NULL;
		inline static mcpwm_gen_handle_t generatorH = NULL;
		inline static mcpwm_gen_handle_t generatorT = NULL;

		// PCNT variables
		inline static pcnt_unit_handle_t pcntUnitH = NULL;
		inline static pcnt_unit_handle_t pcntUnitT = NULL;
		inline static pcnt_channel_handle_t pcntChanH = NULL;
		inline static pcnt_channel_handle_t pcntChanT = NULL;
		inline static QueueHandle_t pcntQueueH;
		inline static QueueHandle_t pcntQueueT;


		void initMCPWN();
		void initPCNT();

		bool setStepper1Command(int32_t steps, float rpm);
		bool setStepper2Command(int32_t steps, float rpm);
		bool setSteppersCommand(int32_t steps1, float rpm1, int32_t steps2, float rpm2);

};

extern StepperHal steppers;


#endif /* !STEPPER_HAL_H */
