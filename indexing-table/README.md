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

mechanické díly: turtle3d

## Mechanické díly
* řemenice:
	* potřeba celkově 4
	* dratek
		* 16, 20, zubů GT2 oboje v 5mm hřídel
		* https://dratek.cz/arduino/149622-remenice-gt2-16-zubu-pro-remen-6mm-vnitrni-prumer-5mm.html (16 zubů, 35kč)
		* https://dratek.cz/arduino/149625-remenice-gt2-20-zubu-pro-remen-6mm-vnitrni-prumer-5mm.html  (20 zubů, 45 kč)
	* 3dfoxshop
		* 20, 30 zubů GT2 oboje v 5mm hřídel
		* https://www.3dfoxshop.cz/remenice-gt2-30-zubu/ (30 zubů, 98kč)
		* https://www.3dfoxshop.cz/remenice-gt2-2-20zubu-d8/  (20 zubů, 55kč)
		* https://www.3dfoxshop.cz/remenice-gt2-36-zubu/ (36 zubů, 98kč)
	* turtle3d <-----
		* široký výběr, provedení v 8mm
		* https://www.turtle3d.cz/cz/490/26/remenice-pro-gt2-6mm-60-zub-dira-8mm (20 zubů, 35kč)
		* https://www.turtle3d.cz/cz/489/26/remenice-pro-gt2-6mm-40-zub-dira-8mm  (40 zubů, 63kč)
		* https://www.turtle3d.cz/cz/487/26/remenice-pro-gt2-6mm-20-zub-dira-8mm (60 zubů, 63kč)
* ložiska
	* 2 ložiska na zařízení nákolu, 1 jako podpora rotace v horizontální úrovni
	* dratek
		* - špatná nabýdka
	* 3dfoxshop
		* https://www.3dfoxshop.cz/lozisko-625zz/ (5mm, 16mm, 30kč)
		* https://www.3dfoxshop.cz/lozisko-608zz/ (8mm, 22mm, 26kč)
	* turtle3d  <-----
		* https://www.turtle3d.cz/cz/175/26/lozisko-608zz-8x22x7-  (8mm, 22mm, 15kč)
* řemen
		* turtle3d
			* https://www.turtle3d.cz/cz/344/26/remen-gt2-6mm-280mm (280mm, 35kč)
			* https://www.turtle3d.cz/cz/343/26/remen-gt2-6mm-300mm  (300mm, 40kč)
			* https://www.turtle3d.cz/cz/342/26/remen-gt2-6mm-400mm (400mm, 45kč)

---> turtle3d jako jediný dodavatel

## Elektrické díly
* potřeba: krokový motor, řadič pro krokový motor, mikrospínač, optická závora
* beru ze svého: stepdown, mikrokontrolér, díly na propojení
* potřeba eventuálně až se bude dělat deska: systém pro vypínaní napájení stepper motorů, přepěťová ochrana, sjednotit propoje na JST

* konektory
	* https://dratek.cz/arduino/3212-svorka-na-dps-3-piny-2.54mm.html (svorka na 3 piny)
	* https://dratek.cz/arduino/5465-svorka-na-dps-4-piny-2.54mm.html (svorka na 4 piny)
	* https://dratek.cz/arduino/122174-jst-ph-2.0mm-4-pin-konektor-na-dps-rovny.html (4 pin JST konektor pro krokové motory)
* krokový motor
	* dratek
		* https://dratek.cz/arduino/48394-krokovy-motor-nema17-47mm-42hd6021-03.html (47mm, 500mNm)
		* https://dratek.cz/arduino/48391-krokovy-motor-nema17-33mm-42hd2037-01.html  (33mm, 280mNm)
* řadič pro krokový motor
	* dratek
		* https://dratek.cz/arduino/1133-motor-driver-a4988-pro-reprap-3d-tiskarny.html (A4988, umí zapojení přes dva vodiče)
* mikrospínač
	* dratek
		* https://dratek.cz/arduino/1113-koncovy-doraz-pakovy-s-kladkou-5a-125v.html (klidně 2x)
* optická závora
	* dratek
		*  https://dratek.cz/arduino/1420-opticky-endstop-spinac-pro-cnc-3d-tiskarny-reprap-makerbot-prusa-mendel-ramps-1.4.html
