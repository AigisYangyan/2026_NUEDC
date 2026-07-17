/**
 * @file    chassis.c
 * @brief   底盘速度环服务实现——采样触发 → 双轮增量 PID → 电机输出。
 *
 * 数据链（§8.2 登记）：
 *   BoardGpio 原始计数 → Encoder_Update(真实 elapsed)/GetSnapshot [m/s]
 *   → Pid_UpdateIncremental(目标 m/s, 反馈 m/s) [±1000 PWM]
 *   → Motor_SetOutput → Motor_Update(elapsed)（slew/换向/超时在 Driver 内）
 */
#include "app/service/chassis/chassis.h"

#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/motor/motor.h"
#include "middleware/pid/pid.h"

/* 控制周期：沿用原速度环 10ms 口径 */
#define CHASSIS_CONTROL_PERIOD_MS 10u

/* 服务私有运行状态 */
static Pid_T s_pid[CHASSIS_SIDE_COUNT];
static float s_target_mps[CHASSIS_SIDE_COUNT];
static Encoder_Snapshot s_snapshot;
static uint32_t s_period_base_ms;

/* 初始 PID 配置：增益 0（调参前不出力），限幅 = 电机口径，微分不过滤 */
static const Pid_Config_T k_pid_cfg = {
    .kp = 0.0f, .ki = 0.0f, .kd = 0.0f,
    .out_limit = (float)MOTOR_OUTPUT_MAX,
    .integral_limit = 0.0f,    /* ≤0 → 按 out_limit 推导（Pid 模块约定） */
    .d_filter_alpha = 1.0f,
};

/* 左右语义映射：服务枚举 → Driver 枚举（唯一映射点） */
static Encoder_Id chassis_encoder_id(Chassis_Side side)
{
    return (side == CHASSIS_SIDE_LEFT) ? ENCODER_LEFT : ENCODER_RIGHT;
}

static Motor_Id chassis_motor_id(Chassis_Side side)
{
    return (side == CHASSIS_SIDE_LEFT) ? MOTOR_LEFT : MOTOR_RIGHT;
}

void Chassis_Init(void)
{
    Chassis_Side side;

    for (side = CHASSIS_SIDE_LEFT; side < CHASSIS_SIDE_COUNT; side++) {
        Pid_Init(&s_pid[side], &k_pid_cfg);
        s_target_mps[side] = 0.0f;
    }
    s_snapshot = (Encoder_Snapshot){ 0 }; /* 快照下标空间是 Encoder_Id，整体清零避免混用 */
    s_period_base_ms = Clock_NowMs();
}

void Chassis_SetSpeedGains(Chassis_Side side, float kp, float ki, float kd)
{
    if (side >= CHASSIS_SIDE_COUNT) {
        return;
    }
    Pid_SetGains(&s_pid[side], kp, ki, kd);
}

void Chassis_SetTargetMps(float left_mps, float right_mps)
{
    s_target_mps[CHASSIS_SIDE_LEFT] = left_mps;
    s_target_mps[CHASSIS_SIDE_RIGHT] = right_mps;
}

void Chassis_Update(void)
{
    uint32_t now_ms = Clock_NowMs();
    uint32_t elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */
    Chassis_Side side;

    if (elapsed_ms < CHASSIS_CONTROL_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    if (!Encoder_Update(elapsed_ms)) {
        /* 采样失败：不刷新电机命令；仍推进 Motor 状态机，让 Driver 的
         * 命令超时保护继续计时（持续失败 → Driver 100ms 后自动归零）。 */
        Motor_Update(elapsed_ms);
        return;
    }
    Encoder_GetSnapshot(&s_snapshot);

    for (side = CHASSIS_SIDE_LEFT; side < CHASSIS_SIDE_COUNT; side++) {
        float out = Pid_UpdateIncremental(
            &s_pid[side],
            s_target_mps[side],
            s_snapshot.speed_mps[chassis_encoder_id(side)]);

        (void)Motor_SetOutput(chassis_motor_id(side), (int16_t)out);
    }
    Motor_Update(elapsed_ms);
}

void Chassis_Stop(void)
{
    Chassis_Side side;

    for (side = CHASSIS_SIDE_LEFT; side < CHASSIS_SIDE_COUNT; side++) {
        s_target_mps[side] = 0.0f;
        Pid_Reset(&s_pid[side]);
    }
    Motor_BrakeAll();
}

void Chassis_GetTelemetry(Chassis_Telemetry_T *out)
{
    Chassis_Side side;
    Pid_Telemetry_T pid_tele;

    for (side = CHASSIS_SIDE_LEFT; side < CHASSIS_SIDE_COUNT; side++) {
        out->target_mps[side] = s_target_mps[side];
        out->feedback_mps[side] = s_snapshot.speed_mps[chassis_encoder_id(side)];
        Pid_GetTelemetry(&s_pid[side], &pid_tele);
        out->pid_out[side] = pid_tele.out;
    }
}
