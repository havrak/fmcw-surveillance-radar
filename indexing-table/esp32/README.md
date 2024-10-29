# ESP32 firmware to control indexing table
As of now code relies on proprietary libraries that are not included in this repository. After the project is closed to being finished, these libraries will be stripped to their bare minimum and included in this repository.

Following codebase is written entirely for esp-idf framework. While hal layer used is mimicking arduino, there are differences and thus simple porting of the code to arduino is not possible.
Configuration of the device is done primarily with Kconfig under esp-idf. While runtime configuration with storing to flash is possible, it is not needed for the current application and would result in unnecessary slowdowns.

# Communication

## Device configuration
* device is configured using AT commands, that are sent to the device over serial connection or MQTT
* majority of configuration is hardcoded in the firmware and can be changed only by using Kconfig and recompiling the firmware
* WARN: as motor control needs to be as efficient as possible these settings might be dropped in the future
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
* G90: sets absolute positioning
* G91: sets relative positioning
* G92: set current position as home
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
	* M03:  Start spindle mode
		* S<SPEED> - fallback speed
		* SH<SPEED> - speed for motor in horizontal plane
		* H<+/-> - start spin clockwise/anticlockwise
		* ST<SPEED> - speed for tilt motor in tilt axis
		* T<+/-> - start spin clockwise/anticlockwise
	* M05:  Stop spindle spinning.
	* can be followed with S<rpm> to set speed

### Programming movements
* device allows to preprogramm sequence of movements that can be executed with one command, or looped continuously
* programms are not stored in flash, but in ram for now. Permanent storage of movements is done in MATLAB's programm configuration file
* P0 - stop executing programm
* P1 <command_id> - run command with given id
* P90 <command_id> - start programming mode with given id
	* if programm already exists it will be overwritten, changing programs post creation is not possible
	* upon entering programming mode all commands are stashed and not executed
	* programming begins with header declaration
		* commands declared in header are executed before any other commands
		* if programm is looped, header will be executed only once
		* can be used to set initial position, units, positioning mode, or home the device
		* P98: declares looped programm, these programs will loop indefinitely until stopped
	* upon call of P91 command programming mode will advance to next step - declaring main loop


#### Example of programming

| Command | Mode               | Description                                  |
|---------|--------------------|----------------------------------------------|
| P1 1    | Programming-header | start programming mode with programm id of 1 |
| G90     | Programming-header | set absolute positioning                     |
| G20     | Programming-header | set units to degrees                         |
| G92     | Programming-header | set current position as home                 |
| P98     | Programming-header | declare looped programm                      |



## Position Uplink from device (NOT FINAL)
* WARN: MQTT uplink will not be implemented, it's not needed for the current application
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


* -> enter programming mode
	* needs to give number to programm
	* if in programming mode G0, M03, M04 command will not be executed, but instead stashed
	* switching relative absolute positioning is not enabled in programming mode
	* as opposed to normal regime pauses can be programmed in
	* programming mode is eneded with given command
