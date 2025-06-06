# ESP32 firmware to control indexing table
As of now code relies on proprietary libraries that are not included in this repository. After the project is closed to being finished, these libraries will be stripped to their bare minimum and included in this repository.

Following codebase is written entirely for esp-idf framework. While hal layer used is mimicking arduino, there are differences and thus simple porting of the code to arduino is not possible.
Configuration of the device is done primarily with Kconfig under esp-idf. While runtime configuration with storing to flash is possible, it is not needed for the current application and would result in unnecessary slowdowns.

# Constants
* these parameters are defined in Kconfig and will require recompilation of the firmware to change
* STEPPER_Y_STEP_COUNT, STEPPER_P_STEP_COUNT - number of steps in a single rotation on horizontal and pitch
	* can be changed on the fly with M92 command, however changes are not persistent
* STEPPER_Y_PIN_DIR, STEPPER_Y_PIN_STEP, STEPPER_P_PIN_DIR, STEPPER_P_PIN_STEP - pins used for direction and step signals
* STEPPER_Y_PIN_ENDSTOP, STEPPER_P_PIN_ENDSTOP - pins used for endstop signals
* STEPPER_MAX_SPEED - maximum speed in rpm
* STEPPER_DEFAULT_SPEED - default speed in rpm
* STEPPER_MIN_SPINDLE_TIME - minimum time in ms between two steps in spindle Mode
	* needed to maintain valid information about current position
	* never set bellow ~30 ms
* STEPPER_HAL_TIMER_PERIOD  - timer period (number of ticks per 1 period)
* STEPPER_HAL_TIMER_RESOLUTION - timer resolution (frequency of the timer)
	* these two influence how quickly will the 16 bit timer fill up, for higher RMP period must be lower, for lower RPM period must be higher


# Features
* two different units: degrees and steps
* two different positioning modes: absolute and relative
* common commands like simple movement, homing, setting speed and such, turning on/off are supported
* non standard GCode commands
	* imposing limits on movement - allowed positions of the steppers can be restricted
	* spindle mode - steppers can freely transitions between acting as steppers and as spindles (continuos motion)
	* programming - user can program in sequence of movements that can be executed with one command
		* these can be looped indefinitely or use simple one layer deep for loop
	* maintaining synchronization between two steppers
		* if both steppers are moving, next command is executed only after both steppers reach their target

## Behavior of limits
* user can impose limits on movement of both steppers this can be done via M201 command
* if user wishes to disable limits, they can do so with M202 Command
* movement must be restricted from two sides, restricting only one side doesn't make any sense as we are spinning in a circle
* limits are within single rotation of stepper, it is impossible to allow limited number of full rotations
* there are two possible basic setting for angles
	* low < high - we are restricted to range [low, high]
	* low > high - we are restricted to range [low, 360] U [0, high]

### Absolute positioning
* all angles are automatically normalized to [0, 360] range, or [0, STEP_COUNT] if we are in steps mode
* no limits
	* stepper will take shortest path to the target
* range: [low, high]
	* if target is outside of the range, it will be adjusted to the closest limit
	* than movement is rather straightforward as we can move only in one direction
* range: [low, 360] U [0, high]
	* if target is outside of the range, it will be adjusted to the closest limit
	* we firstly calculate average of the two limits than:
		* anything in [0, avg] is adjusted to high
		* anything in [avg, 360] is adjusted to low
	* again only one direction of movement is possible


### Relative postioning
* number of steps in single command is limited to +-32767, this is hardware/esp-idf private API limitation and cannot be changed
* no limits
	* stepper will do as many steps as are requested (limited to +-32767)
* range: [low, high]
	* if target is outside of the range, it will be adjusted to the closest limit
* range: [low, 360] U [0, high]
	* if target is outside of the range, it will be adjusted to the closest limit
	* if we are decrementing the angle we can only go to high limit
	* if we are incrementing the angle we can only go to low limit

# Communication

WARNING: DO NOT use this device without first reading the documentation. Commands might have similar names and structures as in common G-code but their behavior can be radically different as this device has rather specific requirements.

## Motor control
* axis descriptors: Y for yaw (horizontal) and P for pitch (tilt)
* M80: turn on high voltage power supply
* M81: turn off high voltage power supply
* M82: stop steppers and clear queues
* G20: set units to degrees
* G21: set units to steps
* G90: absolute positioning
	* Y - set absolute positioning on yaw
	* P - set absolute positioning on pitch
	* NOTE: absolute positioning is not available in spindle mode, if you issue a M03 command to stepper in absolute positioning mode, it will be automatically switched to relative positioning and throw an error
	* if no argument is provided both axis are switched to relative positioning
* G91: sets relative positioning
	* Y - set relative positioning on yaw
	* P - set relative positioning on pitch
	* if no argument is provided both axis are switched to relative positioning
* G92: set current position as 0
	* Y - set current position as 0 on yaw
	* P - set current position as 0 on pitch
	* if no argument is provided both axis are set to 0
	* NOTE: executes only when no other commands are queued in stepper queues
* G28: auto home
	* Y - home yaw
	* P - home pitch
	* if no argument is provided both axis are are homed
* stepper mode
	* only active if spindle mode is off
	* G0: move to/by given angle/steps
		* S<SPEED> - fallback speed
		* SY<SPEED> - speed for yaw motor
		* Y - angle by/to rotate in yaw
		* SP<SPEED> - speed for pitch motor
		* P - angle by/to rotate in pitch
		* speed needs to be provided, if number of steps is set but speed is not ERR 2 is returned
* spindle mode
	* beware when using spindle in programming mode one can desynchronized two command queues, this "issue" will be fixed and is up to user to handle as there is no clear way to decipher user intentions in programm in order to prevent it
	* M03:  Start spindle mode
		* S<SPEED> - fallback speed
		* SY<SPEED> - speed for yaw motor
		* Y<+/-> - start spin with yaw motor clockwise/anticlockwise
		* SP<SPEED> - speed for pitch motor
		* P<+/-> - start spin pitch motor clockwise/anticlockwise
		* NOTE: spindle will always spin at least for STEPPER_MIN_SPINDLE_TIME ms, thus theoretically to spin for N seconds would would have to substract STEPPER_MIN_SPINDLE_TIME from the time you want to spin, however timing here is quite finicky so correction probably will not secure exact number of steps
	* M05:  Stop spindle spinning.
		* Y - stop spindle regime on yaw
		* P - stop spindle regime on pitch
		* NOTE: spindle mode will also automatically end if stepper receives G0 command or if another M03 command is issued
		* if no argument is present neither axis will be stopped
* M92: set step count on steppers
	* Y<STEPS: set step count on Yaw
	* P<STEPS>: set step count on Pitch
	* default step count is defined in Kconfig
* M201: set limits on steppers
	* LY<ANGLE>: low limit on yaw
	* HY<ANGLE>: high limit on horizontal Stepper
	* LP<ANGLE>: low limit on pitch
	* HP<ANGLE>: high limit on pitch
	* ANGLE is given in degrees if unit is set to degrees (0-360), otherwise in steps (0-STEP_COUNT)
	* either no limits are placed on axis or both limits are placed, if user tries to impose just one limit whole axis will not be processed
	* NOTE: keep in mind that if limits are imposed and current postion is not within them, you will have to get the device within limits in one G0 command
* M202: disable limits
	* Y - disable limits on yaw
	* L - disable limits on pitch

### Programming movements
* device allows to preprogram sequence of movements that can be executed with one command, or looped continuously
* programms are not stored in flash, but in ram for now. Permanent storage of movements is done in MATLAB's programm configuration file
* if programm requires movement on both axis it is executed in parallel and next command is executed only after both axis reach their target
* program structures
	* header declaration
		* executed only once
		* used to set initial position, units, positioning mode, or home the device
	* main body
		* used for actual movement
		* if command P29 is issued it will be executed in a loop indefinitely loop
		* can be left empty
* P0 - stop executing programm (only one programm can be active at one time)
* P1 <command_id> - run command with given id
* P2 <command_id - delete command with given id
* P90 <command_id> - start programming mode with given id
	* if programm already exists it will be overwritten, changing programs post creation is not possible
	* upon entering programming mode all commands are stashed and not executed
	* programming begins with header declaration
		* commands declared in header are executed before any other commands
		* if programm is looped, header will be executed only once
		* can be used to set initial position, units, positioning mode, or home the device
* P91: called within programming context, advances programming from header declaration to main body
* P92: ends programming of the main body
	* NOTE: if loops are not closed, programm will not be saved and P92 command is disregarded
* P21: for loop declaration
	* I<ITERATIONS> - number of iterations for loop will take
* P22: end of loop
	* if iterations are not exhausted, programm will jump back to P21 (respectively command right after it)
* P29: declares looped programm, these programs will loop indefinitely until stopped
	* NOTE: only applicable in header declaration
* W0: wait command (seconds)
	* Y<TIME> - wait on yaw
	* P<TIME> - wait on pitch
* W1: wait command (milliseconds)
	* Y<TIME> - wait on yaw
	* P<TIME> - wait on pitch


#### Example of programming

| Command       | Mode                   | Description                                  |
|---------------|------------------------|----------------------------------------------|
| P90 prog      | General -> Header dec  | start programming mode with programm id of 1 |
| G91           | Header dec             | set relative positioning                     |
| G20           | Header dec             | set units to degrees                         |
| G92           | Header dec             | set current position as home                 |
| P29           | Header dec             | declare looped programm                      |
| M03 SY6 Y+    | Header dec -> Main dec | set yaw to be in spindle mode   |
| P91           | Main dec               | start declaration of main loop               |
| G0 SP30 P100  | Main dec               | move pitch by 100 degrees at 30 rpm     |
| G0 SP30 P-100 | Main dec               | move pitch by -100 degrees at 30 rpm    |
| P92           | Main dec               | end programming                              |


### Special advanced commands
* WARN: usage of these commands is not recommended and can lead to unexpected behavior
* their primary usecase is to enable real time control over the device - for example to do tracking of some object
* M82: stop steppers and clear queues
	* similar to M80, however stop command is issued to the steppers instead of shutting them down
	* this means steppers will finish their current movement and then stop - thus data about current position should be still valid
	* used in conjunction with G3 to setup environment for real time control
* G3: direct control over steppers
	* G3 command skip whole scheduling routine and put command directly into stepper queues
	* NOTE: limits are not and cannot be checked
	* NOTE: all values are interpreted as steps
	* NOTE: if M82 is issued beforehand than G3 command should not lead to invalidation of current position
	* S<SPEED> - fallback speed
	* SY<SPEED> - speed for yaw
	* Y - angle by/to rotate in yaw
	* SP<SPEED> - speed for pitch
	* P - angle by/to rotate in pitch
* W3: wait in application layer
	* T<TIME> - time to wait for in ms
	* pauses execution in application layer



# Uplink from the device
* !P <TIMESTAMP>, <HORIZONTAL_ANGLE>, <TILT_ANGLE>
	* roughly every 20 ms device sends its current postion up with serial line
* !R OK or !R ERR <CODE>
	* upon receiving command from the host, device will send back an acknowledgement
	* in case of an error reported please consult following table

| Error code | Description                                                                                             |
|------------|---------------------------------------------------------------------------------------------------------|
| 1          | command wasn't able to be decoded                                                                       |
| 2          | command code is valid but it's arguments aren't                                                         |
| 3          | command was processed, should be added to noProgrammQueue but we failed to get a lock                   |
| 4          | command exists but isn't yet supported by the hardware                                                  |
| 5          | we are either running homing or some programm thus new incoming commands will not be process            |
| 6          | command might be fine but code runned into unexpected occurrence                                        |
| 7          | indicated that program has unclosed for loop, it is recommended to delete whole program and start again |
| 8          | command is not valid in current context                                                                 |

