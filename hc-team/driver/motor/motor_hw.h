/**
 * @file    motor_hw.h
 * @brief   Motor Driver 的硬件边界（HAL port）
 *
 * 本头是 motor.c（状态机：slew / 换向死区 / 命令超时，可主机测试）与
 * motor_hw.c（唯一碰 DL HAL 的实现）之间的唯一契约。
 * 主机测试用 tests/host/fake_motor_hw.c 顶替 motor_hw.c。
 *
 * 本头**不属于**公共 API：上层只应包含 motor.h。
 * 同范式另见 driver/gray/gray_port.h。
 *
 * ⚠ 电机安全（AGENTS.md §8.1）：本层只做「照令写寄存器」，
 * 不得在这里加限幅、slew 或换向保护 —— 那些的唯一所有者是 motor.c 的状态机。
 * 在这里再加一层会与它形成第二个所有者（§8.2）。
 */
#ifndef HC_TEAM_DRIVER_MOTOR_MOTOR_HW_H
#define HC_TEAM_DRIVER_MOTOR_MOTOR_HW_H

#include "driver/motor/motor.h"
#include <stdint.h>

void motor_hw_write_dir(Motor_Id id, int8_t direction);
void motor_hw_write_duty_permille(Motor_Id id, uint16_t duty_permille);
void motor_hw_brake_pins(Motor_Id id);
void motor_hw_start_pwm(void);

#endif
