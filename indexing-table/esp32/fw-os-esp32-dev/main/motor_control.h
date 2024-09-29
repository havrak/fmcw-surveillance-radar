/*
 * motor_control.h
 * Copyright (C) 2024 Havránek Kryštof <krystof@havrak.xyz>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef MOTOR_CONTROLL_H
#define MOTOR_CONTROLL_H

#include <esp_log.h>
#include <per_stepper_driver.h>

class MotorControl{
	private:
	constexpr static char TAG[] = "MotorControl";
	static MotorControl* instance;

	PerStepperDriver* rotation = nullptr;
	PerStepperDriver* tilt = nullptr;

	MotorControl();

	public:



	static MotorControl* getInstance()
	{
		if(instance == nullptr)
		{
			instance = new MotorControl();
		}
		return instance;
	};

	void setMotors(PerStepperDriver* rotation, PerStepperDriver* tilt)
	{
		this->rotation = rotation;
		this->tilt = tilt;
	};



};

#endif /* !MOTOR_CONTROLL_H */
