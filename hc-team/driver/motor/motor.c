/**
 * @file    motor.c
 * @brief   双路直流电机 Driver 状态机实现。
 *
 * 本文件只保留与执行状态机相关的纯 C 逻辑：
 * - 目标输出登记
 * - slew 限速推进
 * - 换向过零与死区
 * - 命令超时归零
 *
 * 所有 TI / DL HAL 访问都封装在 motor_hw.c 中。
 */

#include "motor.h"

#include "driver/motor/motor_hw.h"
#include <limits.h>

typedef struct {
    int16_t target_output;
    int16_t applied_output;
    uint32_t command_age_ms;
    uint32_t deadtime_remaining_ms;
} MotorState;

/*
 * T2 继续沿用 P3.T1 已通过主机测试的占位安全常量：
 * - reverse deadtime = 5 ms
 * - command timeout  = 100 ms
 * - slew            = 100 permille/ms
 * 实际值留待 T3 / 用户硬件口径确定。
 */
enum {
    MOTOR_REVERSE_DEADTIME_MS = 5,
    MOTOR_COMMAND_TIMEOUT_MS = 100,
    MOTOR_SLEW_PER_MS = 100
};

static MotorState s_motors[MOTOR_COUNT];

static int8_t motor_sign(int16_t value)
{
    if (value > 0) {
        return 1;
    }
    if (value < 0) {
        return -1;
    }
    return 0;
}

static uint16_t motor_abs_permille(int16_t value)
{
    return (uint16_t)((value < 0) ? -value : value);
}

static uint32_t motor_add_saturated_u32(uint32_t lhs, uint32_t rhs)
{
    if ((UINT32_MAX - lhs) < rhs) {
        return UINT32_MAX;
    }
    return lhs + rhs;
}

static int16_t motor_step_toward(int16_t current, int16_t target, uint32_t elapsed_ms)
{
    int32_t step = (int32_t)elapsed_ms * MOTOR_SLEW_PER_MS;
    int32_t next = current;

    if (step <= 0) {
        return current;
    }

    if (current < target) {
        next += step;
        if (next > target) {
            next = target;
        }
    } else if (current > target) {
        next -= step;
        if (next < target) {
            next = target;
        }
    }

    return (int16_t)next;
}

static void motor_apply_output(Motor_Id id, int16_t output)
{
    motor_hw_write_dir(id, motor_sign(output));
    motor_hw_write_duty_permille(id, motor_abs_permille(output));
}

static void motor_reset_runtime(MotorState *state)
{
    state->target_output = 0;
    state->applied_output = 0;
    state->command_age_ms = MOTOR_COMMAND_TIMEOUT_MS;
    state->deadtime_remaining_ms = 0u;
}

static void motor_stop_stale_output(Motor_Id id, MotorState *state)
{
    state->applied_output = 0;
    state->deadtime_remaining_ms = 0u;
    motor_apply_output(id, 0);
}

/* ---- 公开 API ----------------------------------------------------------- */

void Motor_Init(void)
{
    Motor_Id id;

    for (id = MOTOR_LEFT; id < MOTOR_COUNT; ++id) {
        motor_reset_runtime(&s_motors[id]);
        motor_apply_output(id, 0);
    }

    motor_hw_start_pwm();
}

bool Motor_SetOutput(Motor_Id id, int16_t output)
{
    if ((id < MOTOR_LEFT) || (id >= MOTOR_COUNT)) {
        return false;
    }
    if ((output < -MOTOR_OUTPUT_MAX) || (output > MOTOR_OUTPUT_MAX)) {
        return false;
    }

    s_motors[id].target_output = output;
    s_motors[id].command_age_ms = 0u;
    return true;
}

void Motor_Update(uint32_t elapsed_ms)
{
    Motor_Id id;

    if (elapsed_ms == 0u) {
        return;
    }

    for (id = MOTOR_LEFT; id < MOTOR_COUNT; ++id) {
        MotorState *state = &s_motors[id];
        int8_t current_sign;
        int8_t target_sign;

        state->command_age_ms = motor_add_saturated_u32(state->command_age_ms, elapsed_ms);
        if (state->command_age_ms >= MOTOR_COMMAND_TIMEOUT_MS) {
            motor_stop_stale_output(id, state);
            continue;
        }

        if (state->deadtime_remaining_ms > 0u) {
            motor_apply_output(id, 0);
            if (elapsed_ms >= state->deadtime_remaining_ms) {
                state->deadtime_remaining_ms = 0u;
            } else {
                state->deadtime_remaining_ms -= elapsed_ms;
            }
            continue;
        }

        current_sign = motor_sign(state->applied_output);
        target_sign = motor_sign(state->target_output);

        if ((current_sign != 0) && (target_sign != 0) && (current_sign != target_sign)) {
            state->applied_output = motor_step_toward(state->applied_output, 0, elapsed_ms);
            motor_apply_output(id, state->applied_output);
            if (state->applied_output == 0) {
                state->deadtime_remaining_ms = MOTOR_REVERSE_DEADTIME_MS;
            }
            continue;
        }

        state->applied_output = motor_step_toward(state->applied_output,
                                                  state->target_output,
                                                  elapsed_ms);
        motor_apply_output(id, state->applied_output);
    }
}

void Motor_Brake(Motor_Id id)
{
    if ((id < MOTOR_LEFT) || (id >= MOTOR_COUNT)) {
        return;
    }

    motor_reset_runtime(&s_motors[id]);
    motor_hw_brake_pins(id);
    motor_hw_write_duty_permille(id, 0u);
}

void Motor_BrakeAll(void)
{
    Motor_Id id;

    for (id = MOTOR_LEFT; id < MOTOR_COUNT; ++id) {
        Motor_Brake(id);
    }
}
