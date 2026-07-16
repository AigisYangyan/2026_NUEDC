#ifndef HC_TEAM_DRIVER_MOTOR_MOTOR_HW_H
#define HC_TEAM_DRIVER_MOTOR_MOTOR_HW_H

#include "driver/motor/motor.h"
#include <stdint.h>

void motor_hw_write_dir(Motor_Id id, int8_t direction);
void motor_hw_write_duty_permille(Motor_Id id, uint16_t duty_permille);
void motor_hw_brake_pins(Motor_Id id);
void motor_hw_start_pwm(void);

#endif
