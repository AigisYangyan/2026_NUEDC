/**
 * @file    tuning_chassis.c
 * @brief   底盘速度环调参 profile：变量组存储 + 注册/重置/应用/刷新。
 *
 * 隔离事实（契约 §9 三原则的落点）：
 * - s_ctx 是本文件私有 static，VOFA 只见它，chassis 内部状态永不注册进 VOFA；
 * - 应用方向唯一：cmd → Chassis_SetSpeedGains / Chassis_SetTargetMps；
 * - tx 只在 RefreshTx 里单向写入：目标/反馈来自 Chassis_GetTelemetry 快照，
 *   增益 kp/ki/kd 来自 cmd 单向回显（应用值恒等 cmd，因 Apply 每拍无条件写）；
 *   无任何反向路径（tx 永不写回 cmd/运行值，隔离本意不变）。W1：pwm 不再外显。
 */
#include "app/service/tuning/tuning_chassis.h"

#include "app/service/chassis/chassis.h"
#include "driver/uart_vofa/uart_vofa.h"

/* 本 profile 的 VOFA 变量组：cmd 为接收绑定目标（vofa_bind_cmd 要求 volatile），
 * tx 为发送通道来源。两组都不与运行变量共存储。 */
typedef struct {
    /* tx：外显通道（注册顺序 = 上位机通道顺序）
     * [0..5] 增益回显（cmd 单向复制）、[6..7] 目标、[8..9] 反馈 */
    float tx_kp_left;
    float tx_ki_left;
    float tx_kd_left;
    float tx_kp_right;
    float tx_ki_right;
    float tx_kd_right;
    float tx_target_left_mps;
    float tx_target_right_mps;
    float tx_feedback_left_mps;
    float tx_feedback_right_mps;
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
    s_ctx.tx_kp_left = 0.0f;
    s_ctx.tx_ki_left = 0.0f;
    s_ctx.tx_kd_left = 0.0f;
    s_ctx.tx_kp_right = 0.0f;
    s_ctx.tx_ki_right = 0.0f;
    s_ctx.tx_kd_right = 0.0f;
    s_ctx.tx_target_left_mps = 0.0f;
    s_ctx.tx_target_right_mps = 0.0f;
    s_ctx.tx_feedback_left_mps = 0.0f;
    s_ctx.tx_feedback_right_mps = 0.0f;
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
    /* tx 注册顺序 = 上位机通道序：增益 L/R → 目标 L/R → 反馈 L/R */
    (void)vofa_register_float(&s_ctx.tx_kp_left);
    (void)vofa_register_float(&s_ctx.tx_ki_left);
    (void)vofa_register_float(&s_ctx.tx_kd_left);
    (void)vofa_register_float(&s_ctx.tx_kp_right);
    (void)vofa_register_float(&s_ctx.tx_ki_right);
    (void)vofa_register_float(&s_ctx.tx_kd_right);
    (void)vofa_register_float(&s_ctx.tx_target_left_mps);
    (void)vofa_register_float(&s_ctx.tx_target_right_mps);
    (void)vofa_register_float(&s_ctx.tx_feedback_left_mps);
    (void)vofa_register_float(&s_ctx.tx_feedback_right_mps);

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
    /* 增益：cmd 单向回显（显示"应用中的设定值"；applied==cmd 因 Apply 每拍写） */
    s_ctx.tx_kp_left = (float)s_ctx.cmd_left_kp;
    s_ctx.tx_ki_left = (float)s_ctx.cmd_left_ki;
    s_ctx.tx_kd_left = (float)s_ctx.cmd_left_kd;
    s_ctx.tx_kp_right = (float)s_ctx.cmd_right_kp;
    s_ctx.tx_ki_right = (float)s_ctx.cmd_right_ki;
    s_ctx.tx_kd_right = (float)s_ctx.cmd_right_kd;
    /* 目标/反馈：Chassis 遥测快照单向副本 */
    s_ctx.tx_target_left_mps = snapshot.target_mps[CHASSIS_SIDE_LEFT];
    s_ctx.tx_target_right_mps = snapshot.target_mps[CHASSIS_SIDE_RIGHT];
    s_ctx.tx_feedback_left_mps = snapshot.feedback_mps[CHASSIS_SIDE_LEFT];
    s_ctx.tx_feedback_right_mps = snapshot.feedback_mps[CHASSIS_SIDE_RIGHT];
}

void TuningChassis_PumpInner(void)
{
    Chassis_Update();
}

void TuningChassis_Exit(void)
{
    Chassis_Stop();
}
