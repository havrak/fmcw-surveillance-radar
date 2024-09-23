file(GLOB main_source
	# "os_core.cpp"
	"*.cpp"
	# "app_main.cpp"
	)

idf_component_register(SRCS ${main_source}
	INCLUDE_DIRS .
	PRIV_REQUIRES
	fw-lib-esp32-wifi-manager
	fw-lib-esp32-mqtt
	fw-lib-esp32-peripherals
	fw-lib-esp32-tasker
	esp_wifi
	json
	esp_timer
	bt
	driver
	esp_event
	mbedtls
	nvs_flash
	app_update
	esp_http_client
	mqtt
	)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20 -Wno-reorder -Wno-class-memaccess -frtti -O2)
