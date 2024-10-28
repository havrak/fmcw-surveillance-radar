# CAD files for the indexing table

## Communication

### Downlink
There are two types of downlink -> AT commands and gcode used to control motion. AT command always have a prefix AT+ and are one per line.

* motor configuration
	* both motors are configured independently
	* motor H - rotation in horizontal plane
		* AT+MOTH? - list configuration for motor A
		* AT+MOTH_SPEED
		* AT+MOTH_LIMIT? - limit information
		* AT+MOTH_LIMIT=y - toggle on limits, by default off
		* AT+MOTH_LIMITMIN=<int> - minimal angle
		* AT+MOTH_LIMITMAX=<int> - maximal angle
	* motor T - tilt of the radar
		* AT+MOTT? - list configuration for motor A
		* AT+MOTT_SPEED
		* AT+MOTT_LIMIT? - limit information
		* AT+MOTT_LIMIT=y - toggle on limits, by default off
		* AT+MOTT_LIMITMIN=<int> - minimal angle
		* AT+MOTT_LIMITMAX=<int> - maximal angle
* control
	* uses custom command strcuture based on gcode
	* in plain gcode its difficult to support both continuos motion and finite positioning with gcode
	* move to absolute position, move to relative position, spin
	* axis descriptors: H for horizontal rotation, T for tilt
	* M80: turn on high voltage power supply
	* M81: turn off high voltage power supply
	* G20: set units to degrees
	* G21: set units to steps
	* G90: sets absolute positioning
	* G91: sets relative positioning
	* G92: set current position as home
	* G28: (without arguments) move to home from current position
	* switch between angle and steps mode
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


## Uplink from device
* when in move send current angle on
	* in message two values are present for every axis
	* absolute -> absolute angle from home position
	* relative -> angle change in relation to last issued command


* -> enter programming mode
	* needs to give number to programm
	* if in programming mode G0, M03, M04 command will not be executed, but instead stashed
	* switching relative absolute positioning is not enabled in programming mode
	* as opposed to normal regime pauses can be programmed in
	* programming mode is eneded with given command
