/**
 * @file    tuning_chassis.c
 * @brief   底盘速度环调参 profile：变量组存储 + 注册/重置/应用/刷新。
 *
 * 隔离事实（契约 §9 三原则的落点）：
 * - s_ctx 是本文件私有 static，VOFA 只见它，chassis 内部状态永不注册进 VOFA；
 * - 应用方向唯一：cmd → Chassis_SetSpeedGains / Chassis_SetTargetMps；
 * - tx 只在 RefreshTx 里被 Chassis_GetTelemetry 快照单向覆盖，无任何反向路径。
 */
#include "app/service/tuning/tuning_chassis.h"

#include "app/service/chassis/chassis.h"
#include "driver/uart_vofa/uart_vofa.h"

/* 本 profile 的 VOFA 变量组：cmd 为接收绑定目标（vofa_bind_cmd 要求 volatile），
 * tx 为发送通道来源。两组都不与运行变量共存储。 */
typedef struct {
    /* tx：遥测快照副本（注册顺序 = 上位机通道顺序） */
    float tx_target_left_mps;
    float tx_target_right_mps;
    float tx_feedback_left_mps;
    float tx_feedback_right_mps;
    float tx_pid_out_left;
    float tx_pid_out_right;
    /* cmd：上位机调参输入 */
    volatile float cmd_target_left_mps;
    volatile float cmd_target_right_mps;
    volatile float cmd_left_kp;
    volatile float cmd_left_ki;
    volatile float cmd_left_kd;
    volatile float cmd_right_kp;
    volatile float cmd_right_ki;
    volatile float cmd_right_kd;
} TuningChassisCtx_T;

static TuningChassisCtx_T s_ctx;

/* 安全初值：全 0（增益 0 = PID 不出力，目标 0 = 静止）。Enter/重进一律执行。 */
static void tuning_chassis_reset_safe(void)
{
    s_ctx.tx_target_left_mps = 0.0f;
    s_ctx.tx_target_right_mps = 0.0f;
    s_ctx.tx_feedback_left_mps = 0.0f;
    s_ctx.tx_feedback_right_mps = 0.0f;
    s_ctx.tx_pid_out_left = 0.0f;
    s_ctx.tx_pid_out_right = 0.0f;
    s_ctx.cmd_target_left_mps = 0.0f;
    s_ctx.cmd_target_right_mps = 0.0f;
    s_ctx.cmd_left_kp = 0.0f;
    s_ctx.cmd_left_ki = 0.0f;
    s_ctx.cmd_left_kd = 0.0f;
    s_ctx.cmd_right_kp = 0.0f;
    s_ctx.cmd_right_ki = 0.0f;
    s_ctx.cmd_right_kd = 0.0f;
}

static void tuning_chassis_register_group(void)
{
    (void)vofa_register_float(&s_ctx.tx_target_left_mps);
    (void)vofa_register_float(&s_ctx.tx_target_right_mps);
    (void)vofa_register_float(&s_ctx.tx_feedback_left_mps);
    (void)vofa_register_float(&s_ctx.tx_feedback_right_mps);
    (void)vofa_register_float(&s_ctx.tx_pid_out_left);
    (void)vofa_register_float(&s_ctx.tx_pid_out_right);

    (void)vofa_bind_cmd("LM", &s_ctx.cmd_target_left_mps);
    (void)vofa_bind_cmd("RM", &s_ctx.cmd_target_right_mps);
    (void)vofa_bind_cmd("LP", &s_ctx.cmd_left_kp);
    (void)vofa_bind_cmd("LI", &s_ctx.cmd_left_ki);
    (void)vofa_bind_cmd("LD", &s_ctx.cmd_left_kd);
    (void)vofa_bind_cmd("RP", &s_ctx.cmd_right_kp);
    (void)vofa_bind_cmd("RI", &s_ctx.cmd_right_ki);
    (void)vofa_bind_cmd("RD", &s_ctx.cmd_right_kd);
}

void TuningChassis_Enter(void)
{
    tuning_chassis_reset_safe();
    tuning_chassis_register_group();
    Chassis_Stop();
    TuningChassis_Apply();
}

void TuningChassis_Apply(void)
{
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT,
                          s_ctx.cmd_left_kp, s_ctx.cmd_left_ki, s_ctx.cmd_left_kd);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT,
                          s_ctx.cmd_right_kp, s_ctx.cmd_right_ki, s_ctx.cmd_right_kd);
    Chassis_SetTargetMps(s_ctx.cmd_target_left_mps, s_ctx.cmd_target_right_mps);
}

void TuningChassis_RefreshTx(void)
{
    Chassis_Telemetry_T snapshot;

    Chassis_GetTelemetry(&snapshot);
    s_ctx.tx_target_left_mps = snapshot.target_mps[CHASSIS_SIDE_LEFT];
    s_ctx.tx_target_right_mps = snapshot.target_mps[CHASSIS_SIDE_RIGHT];
    s_ctx.tx_feedback_left_mps = snapshot.feedback_mps[CHASSIS_SIDE_LEFT];
    s_ctx.tx_feedback_right_mps = snapshot.feedback_mps[CHASSIS_SIDE_RIGHT];
    s_ctx.tx_pid_out_left = snapshot.pid_out[CHASSIS_SIDE_LEFT];
    s_ctx.tx_pid_out_right = snapshot.pid_out[CHASSIS_SIDE_RIGHT];
}

void TuningChassis_PumpInner(void)
{
    Chassis_Update();
}

void TuningChassis_Exit(void)
{
    Chassis_Stop();
}
