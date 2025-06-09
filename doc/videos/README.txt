Všechna videa byla pořízena se stejnou konfigurací radaru. Lišil se pouze program na platformě a pak nastavení processingu/vizualizace. Hlavně u Range-Azimuth mapy je nutné podotknout, že bez snímání obrazovky je vizualizace plynulejší. Roli však také hraje problémová hardwarová akcelerace grafiky MATLABu na Linuxu.

* test_RD_kyvadlo - Range-Doppler mapa, před radarem bylo pověšené kyvadlo, které se pomalu hýbalo tam a zpátky na asi 15 cm
* test_RCS_different_scenes - 1D plot s různými scénami, radar jsem otáčel ručně.
* test_3D_dbscan - platforma se kontinuálně točí a naklání se nahoru a dolu, na CFAR datech je ještě puštěn DBSCAN
* test_RA_raw_decay_spread - Range-Azimuth mapa, zobrazení bez výpočtu CFAR, je aplikován spread pattern
* test_RA_raw_decay - Range-Azimuth mapa, zobrazení bez výpočtu CFAR
* test_RA_cfar - Range-Azimuth mapa, zobrazení pouze CFAR


