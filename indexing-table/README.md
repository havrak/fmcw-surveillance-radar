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

	* dratek
		* 16, 20, zubů GT2 oboje v 5mm hřídel
		* https://dratek.cz/arduino/149622-remenice-gt2-16-zubu-pro-remen-6mm-vnitrni-prumer-5mm.html (16 zubů, 35kč)
		* https://dratek.cz/arduino/149625-remenice-gt2-20-zubu-pro-remen-6mm-vnitrni-prumer-5mm.html  (20 zubů, 45 kč)
	* 3dfoxshop
		* 20, 30 zubů GT2 oboje v 5mm hřídel
		* https://www.3dfoxshop.cz/remenice-gt2-30-zubu/ (30 zubů, 98kč)
		* https://www.3dfoxshop.cz/remenice-gt2-2-20zubu-d8/  (20 zubů, 55kč)
		* https://www.3dfoxshop.cz/remenice-gt2-36-zubu/ (36 zubů, 98kč)
	* dratek
		* - špatná nabýdka
	* 3dfoxshop
		* https://www.3dfoxshop.cz/lozisko-625zz/ (5mm, 16mm, 30kč)
		* https://www.3dfoxshop.cz/lozisko-608zz/ (8mm, 22mm, 26kč)

## Mechanické díly
* řemenice:
	* potřeba celkově 4
	* turtle3d <-----
		* široký výběr, provedení v 8mm
		* https://www.turtle3d.cz/cz/490/26/remenice-pro-gt2-6mm-60-zub-dira-8mm (20 zubů, 35kč)
		* https://www.turtle3d.cz/cz/489/26/remenice-pro-gt2-6mm-40-zub-dira-8mm  (40 zubů, 63kč)
		* https://www.turtle3d.cz/cz/487/26/remenice-pro-gt2-6mm-20-zub-dira-8mm (60 zubů, 63kč)
		* kombinace 20-40 dá rozlišení 0.9°, 20-60 dá 0.6°, myslím, že  20-40 bude stačit na nákolon, 20-60 se může použít na rotaci
		* s rychlostí by neměl být problém jde jen o rozlišení
* ložiska
	* 2 ložiska na zařízení náklonu, 1 jako podpora rotace v horizontální úrovni
	* turtle3d  <-----
		* https://www.turtle3d.cz/cz/175/26/lozisko-608zz-8x22x7-  (8mm, 22mm, 15kč)
* řemen
		* turtle3d
			* https://www.turtle3d.cz/cz/344/26/remen-gt2-6mm-280mm (280mm, 35kč)
			* https://www.turtle3d.cz/cz/343/26/remen-gt2-6mm-300mm  (300mm, 40kč)
			* https://www.turtle3d.cz/cz/342/26/remen-gt2-6mm-400mm (400mm, 45kč)
			* musím si dopočítat ještě vzdálenosti, asi jednou 300 a jednou 400mm
* teplem zalisované matice
	* M5: https://www.turtle3d.cz/cz/733/64/matice-na-zalisovani-za-tepla-m5  (konstrukční části)
	* M4: https://www.turtle3d.cz/cz/732/64/matice-na-zalisovani-za-tepla-m4  (možná uchycení stacionárního stepper motoru, uchycení hřídele skrz rotační spojku)
	* M3:
* šroubky
	* uchycení rotační báze na rotoru - M4
		* celková délka: 38mm+
	* uchycení sběrače - M5 (NOTE: opravity chybu)
		* 4mm na sběrači + 10mm na dřáku
	* uchycení držáku sběrače k statické bázi - M5
		* 15mm na držáku sběrače + klidně 15mm
	* sešroubování dvou částí rotační báze
		* M5 šroubky
		* 4x klidně 25mm s matkou
		* 2x asi 20mm bez matky
	* držení stepper motorů
		* 8 M4 šroubků
		* statický stepper (4mm spacer)
		* stapper na rotoru (1cm spacer?)
* 8mm tyčky v libovolném železářství, turtle3d něco nabízí, ale ceny jsou směšné


## Elektrické díly
* potřeba: krokový motor, řadič pro krokový motor, mikrospínač, optická závora, svorky, JST pro krokové motory
* beru ze svého: stepdown, mikrokontrolér, drátky na propojení, pasivní komponenty, LCD?
* potřeba eventuálně až se bude dělat deska: systém pro vypínaní napájení stepper motorů (asi dva FETy), přepěťová ochrana, sjednotit propoje na JST, indikační LED, tlačítko na vypnutí (nebude toho řádově moc)
* kdybychom se dohodli může být LCD na kterém se bude zobrazovat aktuální pohloha + tlačítka na home, stop, atd.

* konektory
	* https://dratek.cz/arduino/3212-svorka-na-dps-3-piny-2.54mm.html (svorka na 3 piny)
	* https://dratek.cz/arduino/5465-svorka-na-dps-4-piny-2.54mm.html (svorka na 4 piny)
	* https://dratek.cz/arduino/122174-jst-ph-2.0mm-4-pin-konektor-na-dps-rovny.html (4 pin JST konektor pro krokové motory)
* krokový motor
	* dratek
		* https://dratek.cz/arduino/48394-krokovy-motor-nema17-47mm-42hd6021-03.html (47mm, 500mNm)
		* https://dratek.cz/arduino/48391-krokovy-motor-nema17-33mm-42hd2037-01.html  (33mm, 280mNm)
		* 280mNm by mělo stačit
		* jestli máte klasické NEMA na katedře klidně můžeme použít ty
* řadič pro krokový motor
	* dratek
		* https://dratek.cz/arduino/1133-motor-driver-a4988-pro-reprap-3d-tiskarny.html (A4988, umí zapojení přes dva vodiče)
		* mají i levnější variatu bez chladiče, já už doma žádné nemám, takže jsem vybral tuto
* mikrospínač
	* homing náklonu, 2 protože není problém ho zničit
	* dratek
		* https://dratek.cz/arduino/1113-koncovy-doraz-pakovy-s-kladkou-5a-125v.html (klidně 2x)
* optická závora
	* homing v horizontální úrovni
	* dratek
		* klidně bych si poradil jen se závorou samotnou ne na nějaké desce, ale tohle je jediné co měl dratek
		*  https://dratek.cz/arduino/1420-opticky-endstop-spinac-pro-cnc-3d-tiskarny-reprap-makerbot-prusa-mendel-ramps-1.4.html (1x)
