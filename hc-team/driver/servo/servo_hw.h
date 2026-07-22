/**
 * @file    servo_hw.h
 * @brief   Servo Driver 的 TI / DL HAL 硬件写层接口（motor_hw 先例）。
 */
#ifndef SERVO_HW_H
#define SERVO_HW_H

#include "driver/servo/servo.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 两路计数器起振（比较=0，无脉冲）。 */
void servo_hw_start(void);

/** @brief 写高电平脉宽（µs→tick 换算按各自 CLK_FREQ，唯一换算点）。 */
void servo_hw_write_pulse_us(Servo_Id id, uint32_t us);

/** @brief 比较=0，停脉冲。 */
void servo_hw_stop_pulse(Servo_Id id);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_HW_H */
