/**
 * @file    speed_loop.c
 * @brief   DEBUG 速度环最小原子化测试服务实现
 *
 * 本模块负责组织左右轮速度环测试所需的数据源、控制链路与 VOFA 遥测。
 *
 * 功能范围：
 * - 接收 VOFA 下发的左右轮目标速度
 * - 调用编码器采样刷新反馈速度
 * - 调用现有增量式 PID 计算左右轮输出
 * - 输出电机 PWM 并回传目标/反馈/PWM 到 VOFA
 *
 * 设计约定：
 * - 目标速度、反馈速度统一使用 m/s
 * - 本模块不做额外滤波，不改动 PID 核心公式
 * - 进入时清零目标与 PID 历史，退出时主动刹车
 */

#include "app/tasks/speed_loop/speed_loop.h"

#include "app/scheduler/vofa_register.h"
#include "driver/encoder/encoder.h"
#include "driver/motor/motor.h"
#include "driver/uart_vofa/uart_vofa.h"
#include "middleware/pid/pid.h"

static Encoder_Snapshot s_encoder_snapshot;

#define SPEED_LOOP_CONTROL_PERIOD_MS 10u

/* ---- 静态辅助函数 ------------------------------------------------------- */

static void speedloop_reset_pid_runtime(PID_T* pid)
{
    if (pid == (PID_T*)0) {
        return;
    }

    pid->target = 0.0f;
    pid->current = 0.0f;
    pid->out = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->last2_error = 0.0f;
    pid->last_out = 0.0f;
    pid->integral = 0.0f;
    pid->p_out = 0.0f;
    pid->i_out = 0.0f;
    pid->d_out = 0.0f;
    pid->last_d_out = 0.0f;
}

static void speedloop_clear_targets(void)
{
    VofaSpeedLoopCtx_t* ctx = VofaRegister_GetSpeedLoopCtx();

    ctx->cmd_target_left_mps = 0.0f;
    ctx->cmd_target_right_mps = 0.0f;
}

static void speedloop_refresh_telemetry(void)
{
    VofaSpeedLoopCtx_t* ctx = VofaRegister_GetSpeedLoopCtx();

    ctx->tx_target_left_mps = (float)ctx->cmd_target_left_mps;
    ctx->tx_target_right_mps = (float)ctx->cmd_target_right_mps;
    ctx->tx_feedback_left_mps = s_encoder_snapshot.speed_mps[ENCODER_LEFT];
    ctx->tx_feedback_right_mps = s_encoder_snapshot.speed_mps[ENCODER_RIGHT];
    ctx->tx_pwm_left = g_tLeftMotorPID.out;
    ctx->tx_pwm_right = g_tRightMotorPID.out;
}

static void speedloop_sync_pid_cfg(void)
{
    VofaSpeedLoopCtx_t* ctx = VofaRegister_GetSpeedLoopCtx();

    g_tLeftMotorPID.kp = ctx->cmd_left_kp;
    g_tLeftMotorPID.ki = ctx->cmd_left_ki;
    g_tLeftMotorPID.kd = ctx->cmd_left_kd;
    g_tRightMotorPID.kp = ctx->cmd_right_kp;
    g_tRightMotorPID.ki = ctx->cmd_right_ki;
    g_tRightMotorPID.kd = ctx->cmd_right_kd;
}

/* ---- 公开 API ----------------------------------------------------------- */

void SpeedLoop_Init(void)
{
    /* 仅做一次性调试状态初始化，不在这里注册运行态 profile。 */
    speedloop_clear_targets();
    Encoder_GetSnapshot(&s_encoder_snapshot);
    speedloop_refresh_telemetry();
}

void SpeedLoop_Enter(void)
{
    /*
     * Enter 只做“运行时复位”，不做模块二次初始化。
     * 编码器、VOFA、Motor 等基础模块初始化仍由 SysInit() 统一完成。
     */
    speedloop_clear_targets();
    Encoder_GetSnapshot(&s_encoder_snapshot);
    speedloop_reset_pid_runtime(&g_tLeftMotorPID);
    speedloop_reset_pid_runtime(&g_tRightMotorPID);
    Motor_BrakeAll();
    VofaRegister_EnterProfile(VOFA_PROFILE_SPEED_LOOP);
    speedloop_sync_pid_cfg();
    speedloop_refresh_telemetry();
}

void SpeedLoop_Exit(void)
{
    /* 退出时清零控制状态并主动刹车，避免离开测试页后继续输出。 */
    speedloop_clear_targets();
    speedloop_reset_pid_runtime(&g_tLeftMotorPID);
    speedloop_reset_pid_runtime(&g_tRightMotorPID);
    Motor_BrakeAll();
    speedloop_refresh_telemetry();
    VofaRegister_ExitProfile();
}

void SpeedLoop_Sample10ms(void)
{
    /* Task_EncoderSpeedSample 已先更新唯一 Encoder 状态；这里只取值快照。 */
    Encoder_GetSnapshot(&s_encoder_snapshot);
}

void SpeedLoop_Control10ms(void)
{
    VofaSpeedLoopCtx_t* ctx = VofaRegister_GetSpeedLoopCtx();
    float left_out;
    float right_out;

    speedloop_sync_pid_cfg();

    /* 速度环核心继续复用现有增量式 PID 实现。 */
    pid_closeloop_motor((float)ctx->cmd_target_left_mps,
                        (float)ctx->cmd_target_right_mps,
                        s_encoder_snapshot.speed_mps[ENCODER_LEFT],
                        s_encoder_snapshot.speed_mps[ENCODER_RIGHT],
                        &left_out,
                        &right_out);

    (void)Motor_SetOutput(MOTOR_LEFT, (int16_t)left_out);
    (void)Motor_SetOutput(MOTOR_RIGHT, (int16_t)right_out);
    Motor_Update(SPEED_LOOP_CONTROL_PERIOD_MS);
}

void SpeedLoop_Telemetry20ms(void)
{
    /* 遥测只负责刷新观测量并推进 VOFA 发送，不夹带其它业务。 */
    speedloop_refresh_telemetry();
    vofa_run();
}
