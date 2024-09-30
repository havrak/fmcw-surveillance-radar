/*
 * motor_control.cpp
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#include "motor_control.h"

MotorControl* MotorControl::instance = nullptr;

MotorControl::MotorControl()
{
}


void MotorControl::parseGcode(const char* gcode, uint16_t length)
{
	ESP_LOGE(TAG, "Gcode parsign not implemented");
}
