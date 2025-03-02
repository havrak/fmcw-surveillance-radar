file(GLOB main_source
	"*.cpp"
	"stepper_control/*.cpp"
	"stepper_control/*.c"
	)

idf_component_register(SRCS ${main_source}
	INCLUDE_DIRS . stepper_control
	PRIV_REQUIRES
	esp_timer
	driver
	esp_event
	nvs_flash
	)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20 -Wno-reorder -Wno-class-memaccess -frtti -O2)
