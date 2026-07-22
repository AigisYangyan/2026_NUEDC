/**
 * @file    fake_servo_hw.c
 * @brief   servo_hw 的主机侧 fake：记录起振/脉宽/写次数（硬件边界伪装，
 *          fake_motor_hw 同款先例）。
 */
#include "driver/servo/servo_hw.h"

static bool     s_started;
static uint32_t s_pulse_us[SERVO_COUNT];
static uint32_t s_write_count[SERVO_COUNT];

void FakeServoHw_Reset(void)
{
    uint8_t i;

    s_started = false;
    for (i = 0u; i < (uint8_t)SERVO_COUNT; i++) {
        s_pulse_us[i] = 0u;
        s_write_count[i] = 0u;
    }
}

bool FakeServoHw_Started(void)
{
    return s_started;
}

uint32_t FakeServoHw_PulseUs(Servo_Id id)
{
    return s_pulse_us[id];
}

uint32_t FakeServoHw_WriteCount(Servo_Id id)
{
    return s_write_count[id];
}

/* ---- servo_hw API 的 fake 实现 ------------------------------------------ */

void servo_hw_start(void)
{
    uint8_t i;

    s_started = true;
    for (i = 0u; i < (uint8_t)SERVO_COUNT; i++) {
        s_pulse_us[i] = 0u;
    }
}

void servo_hw_write_pulse_us(Servo_Id id, uint32_t us)
{
    s_pulse_us[id] = us;
    s_write_count[id]++;
}

void servo_hw_stop_pulse(Servo_Id id)
{
    s_pulse_us[id] = 0u;
}
