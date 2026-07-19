/**
 * @file    motor_check.c
 * @brief   电机方向自检服务实现：两轮同向 ±200 前/后 2s 循环 + 每拍刷新防超时。
 *
 * 状态机（相位）：
 *   FORWARD --累计≥2000ms--> BACKWARD --累计≥2000ms--> FORWARD --> …（循环，直到 Stop）
 * 每拍：以当前相位 ±MOTOR_CHECK_OUTPUT 刷新两轮命令（Motor_SetOutput 重置 motor.c 命令计龄，
 * 防 100ms 超时归零）→ Motor_Update(elapsed) 推进 slew/换向/死区状态机（单一所有者 motor.c）。
 * 相位翻转只改目标 ±号；FORWARD→BACKWARD 的降到零 + 5ms 死区 + 反向 ramp 全由 motor.c 承担，
 * 本服务零复做（§8.1 单一所有者）。
 */
#include "app/service/motor_check/motor_check.h"

#include "driver/motor/motor.h"

#include <stdbool.h>

#define MOTOR_CHECK_OUTPUT    200    /* ±20% 满量程；换向过零/死区/slew/超时归 motor.c */
#define MOTOR_CHECK_PHASE_MS  2000u  /* 每相时长 */

typedef enum {
    MOTOR_CHECK_FORWARD = 0,
    MOTOR_CHECK_BACKWARD
} motor_check_phase_t;

static motor_check_phase_t s_phase;
static bool     s_seeded;
static uint32_t s_phase_base_ms;
static uint32_t s_last_ms;

static int16_t phase_output(void)
{
    return (s_phase == MOTOR_CHECK_FORWARD) ? (int16_t)MOTOR_CHECK_OUTPUT
                                            : (int16_t)(-MOTOR_CHECK_OUTPUT);
}

/* 两轮同向同幅：一轮反转即该轮 AI1/AI2 接反。 */
static void motor_check_apply_phase(void)
{
    int16_t out = phase_output();

    (void)Motor_SetOutput(MOTOR_LEFT, out);
    (void)Motor_SetOutput(MOTOR_RIGHT, out);
}

void MotorCheck_Start(void)
{
    s_phase = MOTOR_CHECK_FORWARD;
    s_seeded = false;
    s_phase_base_ms = 0u;
    s_last_ms = 0u;
    /* 不立即发命令：首 Update 播种时间基准后再驱动（§8.1 init 安全态）。 */
}

void MotorCheck_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if (!s_seeded) {
        s_seeded = true;
        s_phase_base_ms = now_ms;
        s_last_ms = now_ms;
        motor_check_apply_phase(); /* 起动 FORWARD 目标（hw 于下一拍 Motor_Update 推进） */
        return;
    }

    elapsed_ms = now_ms - s_last_ms; /* 无符号减法处理回绕 */
    s_last_ms = now_ms;

    /* 相位到期翻转（循环）；反向切换的过零 + 死区由 motor.c 状态机承担。 */
    if ((now_ms - s_phase_base_ms) >= MOTOR_CHECK_PHASE_MS) {
        s_phase_base_ms = now_ms;
        s_phase = (s_phase == MOTOR_CHECK_FORWARD) ? MOTOR_CHECK_BACKWARD
                                                   : MOTOR_CHECK_FORWARD;
    }

    motor_check_apply_phase();   /* 每拍刷新命令：重置 motor.c 命令计龄，防 100ms 超时归零 */
    Motor_Update(elapsed_ms);    /* 推进 slew/换向/死区状态机（单一所有者） */
}

void MotorCheck_Stop(void)
{
    Motor_BrakeAll(); /* 确定性停止（§8.1） */
}
