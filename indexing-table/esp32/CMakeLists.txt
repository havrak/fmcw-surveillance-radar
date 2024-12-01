# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.26)

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION.txt PROJECT_VER LIMIT_COUNT 1)
set_property(DIRECTORY . APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/VERSION.txt)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fw-os-esp32)

