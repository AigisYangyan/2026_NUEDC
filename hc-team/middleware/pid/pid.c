/*******************************************************************************
 * @file    pid.c
 * @brief   PID 纯算法模块实现（调用者持有上下文）
 *
 * 功能范围：
 * - 增量式 / 位置式 PID 公式，输出对称限幅、积分限幅、微分一阶低通、
 *   NaN/Inf 输出回退。
 *
 * 不负责的内容（AGENTS.md §3.3 / §8.2）：
 * - 传感器采样、方向修正、单位换算、执行器死区——分别归数据源层与执行层。
 * - 任何模块级实例：上下文一律由调用者持有并显式传入。
 *******************************************************************************/

#include "pid.h"
#include <math.h>

/* ---- 私有函数：算法辅助 -------------------------------------------------- */

/* 对称限幅；abs_limit<=0 表示不限幅 */
static float pid_clamp_symmetric(float value, float abs_limit)
{
    if (abs_limit <= 0.0f) {
        return value;
    }

    if (value > abs_limit) {
        return abs_limit;
    }

    if (value < -abs_limit) {
        return -abs_limit;
    }

    return value;
}

/* 一阶低通；alpha>=1 不过滤，alpha<=0 保持上次值 */
static float pid_apply_first_order_filter(float current, float last, float alpha)
{
    if (alpha <= 0.0f) {
        return last;
    }

    if (alpha >= 1.0f) {
        return current;
    }

    return alpha * current + (1.0f - alpha) * last;
}

/* 积分限幅取值：显式配置优先，否则按输出限幅 *3.5 推导 */
static float pid_get_integral_limit(const Pid_T *pid)
{
    if (pid->cfg.integral_limit > 0.0f) {
        return pid->cfg.integral_limit;
    }

    if (pid->cfg.out_limit > 0.0f) {
        return pid->cfg.out_limit * 3.5f;
    }

    return 0.0f;
}

/* NaN/Inf 输出回退到 fallback；fallback 也无效时回 0 */
static float pid_sanitize_output(float value, float fallback)
{
    if (isnan(value) || isinf(value)) {
        if (isnan(fallback) || isinf(fallback)) {
            return 0.0f;
        }
        return fallback;
    }

    return value;
}

/* 输出限幅 + 有效值保护，并记录回退源 */
static void pid_finalize_output(Pid_T *pid)
{
    pid->out = pid_sanitize_output(pid->out, pid->last_out);
    pid->out = pid_clamp_symmetric(pid->out, pid->cfg.out_limit);
    pid->last_out = pid->out;
}

/* ---- 公共 API：生命周期与配置 -------------------------------------------- */

void Pid_Init(Pid_T *pid, const Pid_Config_T *config)
{
    *pid = (Pid_T){ 0 };
    pid->cfg = *config;
}

void Pid_Reset(Pid_T *pid)
{
    Pid_Config_T cfg = pid->cfg;

    *pid = (Pid_T){ 0 };
    pid->cfg = cfg;
}

void Pid_SetGains(Pid_T *pid, float kp, float ki, float kd)
{
    pid->cfg.kp = kp;
    pid->cfg.ki = ki;
    pid->cfg.kd = kd;
}

void Pid_SetLimits(Pid_T *pid, float out_limit, float integral_limit)
{
    pid->cfg.out_limit = out_limit;
    pid->cfg.integral_limit = integral_limit;
}

/* ---- 公共 API：更新公式 -------------------------------------------------- */

float Pid_UpdateIncremental(Pid_T *pid, float target, float feedback)
{
    float raw_d_out;

    pid->error = target - feedback;

    pid->p_out = pid->cfg.kp * (pid->error - pid->last_error);
    pid->i_out = pid->cfg.ki * pid->error;

    raw_d_out = pid->cfg.kd * (pid->error
        - 2.0f * pid->last_error
        + pid->last2_error);
    pid->d_out = pid_apply_first_order_filter(raw_d_out,
        pid->last_d_out,
        pid->cfg.d_filter_alpha);

    pid->out += pid->p_out + pid->i_out + pid->d_out;

    pid->last2_error = pid->last_error;
    pid->last_error = pid->error;
    pid->last_d_out = pid->d_out;

    pid_finalize_output(pid);
    return pid->out;
}

float Pid_UpdatePositional(Pid_T *pid, float target, float feedback)
{
    float raw_d_out;

    pid->error = target - feedback;
    pid->integral += pid->error;
    pid->integral = pid_clamp_symmetric(pid->integral, pid_get_integral_limit(pid));

    pid->p_out = pid->cfg.kp * pid->error;
    pid->i_out = pid->cfg.ki * pid->integral;

    raw_d_out = pid->cfg.kd * (pid->error - pid->last_error);
    pid->d_out = pid_apply_first_order_filter(raw_d_out,
        pid->last_d_out,
        pid->cfg.d_filter_alpha);

    pid->out = pid->p_out + pid->i_out + pid->d_out;

    pid->last_error = pid->error;
    pid->last_d_out = pid->d_out;

    pid_finalize_output(pid);
    return pid->out;
}

/* ---- 公共 API：遥测 ------------------------------------------------------ */

void Pid_GetTelemetry(const Pid_T *pid, Pid_Telemetry_T *out_telemetry)
{
    out_telemetry->out = pid->out;
    out_telemetry->p_out = pid->p_out;
    out_telemetry->i_out = pid->i_out;
    out_telemetry->d_out = pid->d_out;
}
