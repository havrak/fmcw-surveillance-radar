# Indexing table directory

## Hardware design
* stator level (components on static plane)
	* [X] ESP32 microcontroller
	* [ ] 1x stepper motor driver (try out DVR8833, else use A4988)
	* [X] 1x display info panel (optional)
	* [ ] 1x power supply 9V connected with barrel jack
	* [ ] 1x N channel MOSFET, 1x P channel MOSFET to control power supply (standard grounded load)
	* [ ] 1x stepper motor
* rotor level (components rotating in horizontal axis)
	* [ ] slip ring
	* [X] step down 9V to 5V for Radar
	* [ ] 1x stepper motor driver
	* [ ] 1x stepper motor
