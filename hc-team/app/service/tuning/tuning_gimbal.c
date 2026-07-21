/**
 * @file    tuning_gimbal.c
 * @brief   云台位置环静态调参 profile：变量组存储 + 注册/重置/应用/刷新（W8 契约 §30）。
 *
 * 隔离事实（契约 §9 三原则的落点，同 tuning_chassis）：
 * - s_ctx 是本文件私有 static，VOFA 只见它，gimbal 内部状态永不注册进 VOFA；
 * - 应用方向唯一：cmd → 清洗 → Gimbal_SetAimTuning / Gimbal_ReselectTopic；
 * - tx 只在 RefreshTx 里单向写入：err/delta/cur/state 来自 Gimbal_GetTelemetry 快照，
 *   增益/DB/MS 回显来自 Apply 存下的清洗后应用值 s_applied（清洗唯一点在 Apply，
 *   RefreshTx 不复洗）；无任何反向路径（tx 永不写回 cmd/运行值）。
 * - GO 的消费清 0 是 cmd 所有者（本模块）的内部单次触发语义，非反向数据流。
 */
#include "app/service/tuning/tuning_gimbal.h"

#include "app/service/gimbal/gimbal.h"
#include "driver/uart_vofa/uart_vofa.h"

/* 安全初值（契约 §30）：零出力由 DB=10000 保证（floor-1 下 kp=0 不是零出力）。 */
#define TUNING_GIMBAL_SAFE_DEADBAND_PX 10000.0f
#define TUNING_GIMBAL_SAFE_MAX_STEP    1.0f

/* 本 profile 的 VOFA 变量组：cmd 为接收绑定目标（vofa_bind_cmd 要求 volatile），
 * tx 为发送通道来源。两组都不与运行变量共存储。 */
typedef struct {
    /* tx：外显通道（注册顺序 = 上位机通道顺序，契约 §30）
     * [0..5] 遥测（err/delta/cur ×XY）、[6] 状态、[7..12] cmd 清洗后回显 */
    float tx_err_x;
    float tx_err_y;
    float tx_delta_x;
    float tx_delta_y;
    float tx_cur_x;
    float tx_cur_y;
    float tx_state;
    float tx_kp_x;
    float tx_kd_x;
    float tx_kp_y;
    float tx_kd_y;
    float tx_deadband;
    float tx_max_step;
    /* cmd：上位机调参输入 */
    volatile float cmd_kp_x;
    volatile float cmd_kd_x;
    volatile float cmd_kp_y;
    volatile float cmd_kd_y;
    volatile float cmd_deadband;
    volatile float cmd_max_step;
    volatile float cmd_go;
} TuningGimbalCtx_T;

static TuningGimbalCtx_T s_ctx;
static Gimbal_AimTuning_T s_applied;   /* Apply 清洗后的应用值（回显来源，单一清洗点） */

static void tuning_gimbal_reset_safe(void)
{
    s_ctx.tx_err_x = 0.0f;
    s_ctx.tx_err_y = 0.0f;
    s_ctx.tx_delta_x = 0.0f;
    s_ctx.tx_delta_y = 0.0f;
    s_ctx.tx_cur_x = 0.0f;
    s_ctx.tx_cur_y = 0.0f;
    s_ctx.tx_state = 0.0f;
    s_ctx.tx_kp_x = 0.0f;
    s_ctx.tx_kd_x = 0.0f;
    s_ctx.tx_kp_y = 0.0f;
    s_ctx.tx_kd_y = 0.0f;
    s_ctx.tx_deadband = TUNING_GIMBAL_SAFE_DEADBAND_PX;
    s_ctx.tx_max_step = TUNING_GIMBAL_SAFE_MAX_STEP;
    s_ctx.cmd_kp_x = 0.0f;
    s_ctx.cmd_kd_x = 0.0f;
    s_ctx.cmd_kp_y = 0.0f;
    s_ctx.cmd_kd_y = 0.0f;
    s_ctx.cmd_deadband = TUNING_GIMBAL_SAFE_DEADBAND_PX;
    s_ctx.cmd_max_step = TUNING_GIMBAL_SAFE_MAX_STEP;
    s_ctx.cmd_go = 0.0f;
}

static void tuning_gimbal_register_group(void)
{
    /* tx 注册顺序 = 上位机通道序：err → delta → cur → state → 回显 */
    (void)vofa_register_float(&s_ctx.tx_err_x);
    (void)vofa_register_float(&s_ctx.tx_err_y);
    (void)vofa_register_float(&s_ctx.tx_delta_x);
    (void)vofa_register_float(&s_ctx.tx_delta_y);
    (void)vofa_register_float(&s_ctx.tx_cur_x);
    (void)vofa_register_float(&s_ctx.tx_cur_y);
    (void)vofa_register_float(&s_ctx.tx_state);
    (void)vofa_register_float(&s_ctx.tx_kp_x);
    (void)vofa_register_float(&s_ctx.tx_kd_x);
    (void)vofa_register_float(&s_ctx.tx_kp_y);
    (void)vofa_register_float(&s_ctx.tx_kd_y);
    (void)vofa_register_float(&s_ctx.tx_deadband);
    (void)vofa_register_float(&s_ctx.tx_max_step);

    (void)vofa_bind_cmd("XP", &s_ctx.cmd_kp_x);
    (void)vofa_bind_cmd("XD", &s_ctx.cmd_kd_x);
    (void)vofa_bind_cmd("YP", &s_ctx.cmd_kp_y);
    (void)vofa_bind_cmd("YD", &s_ctx.cmd_kd_y);
    (void)vofa_bind_cmd("DB", &s_ctx.cmd_deadband);
    (void)vofa_bind_cmd("MS", &s_ctx.cmd_max_step);
    (void)vofa_bind_cmd("GO", &s_ctx.cmd_go);
}

/* 负值截 0（kp/kd/DB 的值域下界；§7 外部输入清洗的一半，另一半是 MS floor）。 */
static float tuning_gimbal_clamp_nonneg(float value)
{
    return (value >= 0.0f) ? value : 0.0f;
}

void TuningGimbal_Enter(void)
{
    tuning_gimbal_reset_safe();
    tuning_gimbal_register_group();
    Gimbal_Stop();
    TuningGimbal_Apply();
}

void TuningGimbal_Apply(void)
{
    /* volatile cmd 逐个快照后清洗；清洗唯一点在此（RefreshTx 只回显 s_applied）。 */
    float ms = s_ctx.cmd_max_step;

    s_applied.kp[VISION_AIM_AXIS_X] = tuning_gimbal_clamp_nonneg(s_ctx.cmd_kp_x);
    s_applied.kd[VISION_AIM_AXIS_X] = tuning_gimbal_clamp_nonneg(s_ctx.cmd_kd_x);
    s_applied.kp[VISION_AIM_AXIS_Y] = tuning_gimbal_clamp_nonneg(s_ctx.cmd_kp_y);
    s_applied.kd[VISION_AIM_AXIS_Y] = tuning_gimbal_clamp_nonneg(s_ctx.cmd_kd_y);
    s_applied.deadband_px[VISION_AIM_AXIS_X] = tuning_gimbal_clamp_nonneg(s_ctx.cmd_deadband);
    s_applied.deadband_px[VISION_AIM_AXIS_Y] = s_applied.deadband_px[VISION_AIM_AXIS_X];
    if (ms < 1.0f) {
        ms = 1.0f;      /* vision_aim 前置条件 max_step>=1 */
    }
    s_applied.max_step_pulse[VISION_AIM_AXIS_X] = (int32_t)ms;
    s_applied.max_step_pulse[VISION_AIM_AXIS_Y] = (int32_t)ms;

    Gimbal_SetAimTuning(&s_applied);

    /* GO：≥0.5 边沿一次性消费（先清再触发；重握手失败＝下拍上位机可再按）。 */
    if (s_ctx.cmd_go >= 0.5f) {
        s_ctx.cmd_go = 0.0f;
        (void)Gimbal_ReselectTopic();
    }
}

void TuningGimbal_RefreshTx(void)
{
    Gimbal_Telemetry_T snapshot;

    Gimbal_GetTelemetry(&snapshot);
    s_ctx.tx_err_x = snapshot.last_error_px[VISION_AIM_AXIS_X];
    s_ctx.tx_err_y = snapshot.last_error_px[VISION_AIM_AXIS_Y];
    s_ctx.tx_delta_x = (float)snapshot.last_delta_pulse[VISION_AIM_AXIS_X];
    s_ctx.tx_delta_y = (float)snapshot.last_delta_pulse[VISION_AIM_AXIS_Y];
    s_ctx.tx_cur_x = (float)snapshot.cur_pulse[VISION_AIM_AXIS_X];
    s_ctx.tx_cur_y = (float)snapshot.cur_pulse[VISION_AIM_AXIS_Y];
    s_ctx.tx_state = (float)snapshot.state;
    /* 回显：Apply 清洗后的应用值（DB/MS 双轴同值，回显 X 轴份） */
    s_ctx.tx_kp_x = s_applied.kp[VISION_AIM_AXIS_X];
    s_ctx.tx_kd_x = s_applied.kd[VISION_AIM_AXIS_X];
    s_ctx.tx_kp_y = s_applied.kp[VISION_AIM_AXIS_Y];
    s_ctx.tx_kd_y = s_applied.kd[VISION_AIM_AXIS_Y];
    s_ctx.tx_deadband = s_applied.deadband_px[VISION_AIM_AXIS_X];
    s_ctx.tx_max_step = (float)s_applied.max_step_pulse[VISION_AIM_AXIS_X];
}

void TuningGimbal_PumpInner(void)
{
    Gimbal_Update();
}

void TuningGimbal_Exit(void)
{
    Gimbal_Stop();
}
