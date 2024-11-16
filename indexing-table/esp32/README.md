# ESP32 firmware to control indexing table
As of now code relies on proprietary libraries that are not included in this repository. After the project is closed to being finished, these libraries will be stripped to their bare minimum and included in this repository.

Following codebase is written entirely for esp-idf framework. While hal layer used is mimicking arduino, there are differences and thus simple porting of the code to arduino is not possible.
Configuration of the device is done primarily with Kconfig under esp-idf. While runtime configuration with storing to flash is possible, it is not needed for the current application and would result in unnecessary slowdowns.

# TODO
* reintroduce MQTT as it will not pose any slowdowns with current architecture
* all AT commands need to be redefined as gcode, all will use the M prefix (firmware version, setting limits, system info, etc.)

# Communication
## Device configuration (TO BE SCRAPPED)
* device is configured using AT commands, that are sent to the device over serial connection or MQTT
* majority of configuration is hardcoded in the firmware and can be changed only by using Kconfig and recompiling the firmware
* motor H - rotation in horizontal plane
	* AT+MOTH? - list configuration for motor A
	* AT+MOTH_LIMIT? - limit information
	* AT+MOTH_LIMIT=y - toggle on limits, by default off
	* AT+MOTH_LIMITMIN=<int> - minimal angle
	* AT+MOTH_LIMITMAX=<int> - maximal angle
* motor T - tilt of the radar
	* AT+MOTT? - list configuration for motor A
	* AT+MOTT_LIMIT? - limit information
	* AT+MOTT_LIMIT=y - toggle on limits, by default off
	* AT+MOTT_LIMITMIN=<int> - minimal angle
	* AT+MOTT_LIMITMAX=<int> - maximal angle

## Motor control
* uses custom command strcuture based on gcode
* in plain gcode its difficult to support both continuos motion and finite positioning with gcode
* move to absolute position, move to relative position, spin
* axis descriptors: H for horizontal rotation, T for tilt
* M80: turn on high voltage power supply TODO
* M81: turn off high voltage power supply TODO
* G20: set units to degrees
* G21: set units to steps
* G90: absolute positioning
	* H - set absolute positioning on horizontal axis
	* T - set absolute positioning on tilt axis
	* NOTE: absolute positioning is not available in spindle mode, if you issue a M03 command to stepper in absolute positioning mode, it will be automatically switched to relative positioning and throw an error
* G91: sets relative positioning
	* H - set relative positioning on horizontal axis
	* T - set relative positioning on tilt axis
* G92: set current position as home
	* NOTE: executes only when no other commands are queued in stepper queues
* G28: move to home from current position
* stepper mode
	* only active if spindle mode is off
	* G0: move to/by given angle/steps
		* S<SPEED> - fallback speed
		* SH<SPEED> - speed for motor in horizontal plane
		* H - angle by/to rotate in horizontal plane
		* ST<SPEED> - speed for tilt motor in tilt axis
		* T - angle by/to rotate in tilt axis
	* can be followed with S<rpm> to set speed
* spindle mode
	* beware when using spindle in programming mode one can desynchronized two command queues, this "issue" will be fixed and is up to user to handle as there is no clear way to decipher user intentions in programm in order to prevent it
	* M03:  Start spindle mode
		* S<SPEED> - fallback speed
		* SH<SPEED> - speed for motor in horizontal plane
		* H<+/-> - start spin clockwise/anticlockwise
		* ST<SPEED> - speed for tilt motor in tilt axis
		* T<+/-> - start spin clockwise/anticlockwise
	* M05:  Stop spindle spinning.
		* H - stop spindle regime on horizontal axis
		* T - stop spindle regime on tilt axis
		* NOTE: spindle mode will also automatically end if stepper receives G0 command
	* can be followed with S<rpm> to set speed
* M201: set limits on steppers
	* LH<ANGLE>: low limit on horizontal stepper
	* HH<ANGLE>: high limit on horizontal Stepper
	* LT<ANGLE>: low limit on tilt stepper
	* HT<ANGLE>: high limit on tilt stepper
	* NOTE: keep in mind that if limits are imposed and current postion is not within them, you will have to get the device within limits in one G0 command

### Programming movements (NOT FINAL)
* device allows to preprogram sequence of movements that can be executed with one command, or looped continuously
* programms are not stored in flash, but in ram for now. Permanent storage of movements is done in MATLAB's programm configuration file
* if programm requires movement on both axis it is executed in parallel and next command is executed only after both axis reach their target
* P0 - stop executing programm (only one programm can be active at one time)
* P1 <command_id> - run command with given id
* P90 <command_id> - start programming mode with given id
	* if programm already exists it will be overwritten, changing programs post creation is not possible
	* upon entering programming mode all commands are stashed and not executed
	* programming begins with header declaration
		* commands declared in header are executed before any other commands
		* if programm is looped, header will be executed only once
		* can be used to set initial position, units, positioning mode, or home the device
		* P29: declares looped programm, these programs will loop indefinitely until stopped
* P91: called within programming context, advances programming from header declaration to main body
* P92: ends programming of the main body - code is saved in the memory if all loops are closed
* P21: for loop declaration
	* I<ITERATIONS> - number of iterations for loop will take
* P22 - end of loop
	* if iterations are not exhausted, programm will jump back to P21
* W0: wait command (seconds)
	* H<TIME> - wait on horizontal stepper
	* T<TIME> - wait on tilt stepper
* W1: wait command (miliseconds)
	* H<TIME> - wait on horizontal stepper
	* T<TIME> - wait on tilt stepper


#### Example of programming

| Command       | Mode                   | Description                                  |
|---------------|------------------------|----------------------------------------------|
| P1 1          | General -> Header dec  | start programming mode with programm id of 1 |
| G91           | Header dec             | set relative positioning                     |
| G20           | Header dec             | set units to degrees                         |
| G92           | Header dec             | set current position as home                 |
| P98           | Header dec             | declare looped programm                      |
| M03 SH120 H+  | Header dec -> Main dec | set horizontal motor to be in spindle mode   |
| P91           | Main dec               | start declaration of main loop               |
| G0 ST30 T100  | Main dec               | move tilt motor by 100 degrees at 30 rpm     |
| G0 ST30 T-100 | Main dec               | move tilt motor by -100 degrees at 30 rpm    |
| P99           | Main dec               | end programming                              |





## Position Uplink from device (NOT FINAL)
* WARN: more up to date ideas are in personal notes section
* two possible approaches - every step we transmit current position x we transmit delta in position and time between steps
* should be transmitted in format directly usable by matlab
* transmitting delta in position
	* gives values more related to speed vector that is actually used on the processing side of the device
	* not useful for any applications that don't use continuos motion, but only finite positioning
* transmission of current position
	* sends current position in angle or steps (depends on current unit)
	* values require more postprocessing on the receiving side
	* in message two values are present for every axis
	* absolute and relative positioning needs to be distinguished
		* absolute positioning is always given in absolute angle from home position
		* relative positioning is given in angle change in relation to last issued command


## Personal notes
* optimally there is standard interface to control steppers
	* must be compatible both with Remote Controlled Transceiver approach or MCPWM+PCNT
	* stepper control should be relayed in a form of standardized command
	* needed functionality from HAL
		* [X] motors needs to be step individually or simultaneously
		* [X] spindle regime must be supported directly by the HAL
		* [X] commands can be queued
		* [X] queue must support
			* [X] clearing whole queue
			* [X] get next command
			* [X] get size of queue
		* [X] information about current command must be available
		* [X] timestamp of the start of the current command must be available, only affect commands that move the steppers (used to calculate current position)
		* [X] information about previous command must be available
		* [X] all information will be done stored in relative positioning.
		* [X] all information is stored in steps, coversion to degrees is done in the application layer
		* [X] pause commands must be handled here as application layer will not be synchronized with the motor control
		* [X] empty command must be able to be handled -> if we are issuing commands for both steppers the stepper without any command will have to wait for the second to finish
		* [ ] number of steps taken since start of the command
		 	* [ ] check against finished to speed up calculations
			* [ ] handle stepper and spindle mode
	* application layer functionality
		* [ ] homing
		* [ ] motor task functions
			* [ ] check if there is finished command, if so update base position from it
			* [ ] current position is estimated from the time of start of previous command and the speed of the motor
		* [ ] all position calculations will be done in the application layer
	  * [ ] absolute positioning will be done in the application layer (will require some finicky calculations to be done as each command will need to be adjusted)
		* [ ] limits on steppers are enforced and checked in the application layer (logic is similar to that of converting to absolute positioning)
		* absolute positioning/limit enforcement
			* we will store total number of steps done, register will overflow on number of steps times microstepping
			* this number will be updated after a command has finished execution -- thus we will alway know the current position
			* next command to be executed will need to be checked against end position of the previous one, after that it can either be adjusted (if we are in absolute postioning) or scrapped (if limits are reached)
			* in absolute positioning commands will be adjusted to take the shortest path to the target
			* TODO: consider whether crossing the endstop should always reset the position counter or whether endstops are to be active only in homing mode
		* command parsing
		* programming and programm execution
			* keep in mind that moves must be preschedulde and each iteration of the command loop will need to be corrected individually
		* conversion between steps and degrees
		* reporting of current position -> more in uplink section
	* XXX: limits checking and absolute positioning is not high priority
