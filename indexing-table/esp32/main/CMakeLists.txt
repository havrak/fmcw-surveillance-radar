file(GLOB main_source
	# "os_core.cpp"
	"*.cpp"
	"stepper_control/*.cpp"
	# "app_main.cpp"
	)

idf_component_register(SRCS ${main_source}
	INCLUDE_DIRS . stepper_control
	PRIV_REQUIRES
	json
	esp_timer
	driver
	esp_event
	nvs_flash
	app_update
	esp_http_client
	)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20 -Wno-reorder -Wno-class-memaccess -frtti -O2)
