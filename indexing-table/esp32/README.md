# CAD files for the indexing table

## Communication

### Downlink
There are two types of downlink -> AT commands and gcode used to control motion. AT command always have a prefix AT+ and are one per line.

* motor configuration
	* both motors are configured independently
	* motor R - rotation in horizontal plane
		* AT+MOTR? - list configuration for motor A
		* AT+MOTR_STEP? - get number of steps per rotation
		* AT+MOTR_STEP=<int> - set number of steps per rotation
		* AT+MOTR_GEAR? - get gear ratio on belt drive
		* AT+MOTR_GEAR=<float> - set gear ratio on belt drive
		* AT+MOTR_SPEED
		* AT+MOTR_LIMIT? - limit information
		* AT+MOTR_LIMIT=y - toggle on limits, by default off
		* AT+MOTR_LIMITMIN=<int> - minimal angle
		* AT+MOTR_LIMITMAX=<int> - maximal angle
	* motor T - tilt of the radar
		* AT+MOTT? - list configuration for motor A
		* AT+MOTT_STEP? - get number of steps per rotation
		* AT+MOTT_STEP=<int> - set number of steps per rotation
		* AT+MOTT_GEAR? - get gear ratio on belt drive
		* AT+MOTT_GEAR=<float> - set gear ratio on belt drive
		* AT+MOTT_SPEED
		* AT+MOTT_LIMIT? - limit information
		* AT+MOTT_LIMIT=y - toggle on limits, by default off
		* AT+MOTT_LIMITMIN=<int> - minimal angle
		* AT+MOTT_LIMITMAX=<int> - maximal angle
* control
	* uses custom command strcuture based on gcode
	* in plain gcode its difficult to support both continuos motion and finite positioning with gcode
	* move to absolute position, move to relative position, spin
	* axis descriptors: X for rotation, Y for tilt
	* M80: turn on high voltage power supply
	* M81: turn off high voltage power supply
	* G90: sets absolute positioning
	* G91: sets relative positioning
	* G92: set current position as home
	* G28: (without arguments) move to home from current position
	* S<RPM>: set speed
	* spin - only affect X axis
		* M03:  Start spindle spinning clockwise.
		* M04:  Start spindle spinning anti-clockwise.
		* M05:  Stop spindle spinning.
	* G0: move axis


## Uplink from device
* when in move send current angle on
	* in message two values are present for every axis
	* absolute -> absolute angle from home position
	* relative -> angle change in relation to last issued command


