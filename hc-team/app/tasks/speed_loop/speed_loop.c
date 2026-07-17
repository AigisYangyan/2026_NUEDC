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
#define SPEED_LOOP_PID_OUT_LIMIT 1000.0f    /* PWM 输出尺度 ±1000，沿用原电机实例限幅 */

/* 左右轮速度环 PID 上下文：本任务持有，增益由 VOFA cmd 同步 */
static Pid_T s_left_pid;
static Pid_T s_right_pid;

static const Pid_Config_T s_pid_cfg = {
    .kp = 0.0f, .ki = 0.0f, .kd = 0.0f,
    .out_limit = SPEED_LOOP_PID_OUT_LIMIT,
    .integral_limit = 0.0f,     /* 按 out_limit*3.5 推导 */
    .d_filter_alpha = 1.0f,     /* 不过滤 */
};

/* ---- 静态辅助函数 ------------------------------------------------------- */

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
    Pid_Telemetry_T left_tele;
    Pid_Telemetry_T right_tele;

    Pid_GetTelemetry(&s_left_pid, &left_tele);
    Pid_GetTelemetry(&s_right_pid, &right_tele);
    ctx->tx_pwm_left = left_tele.out;
    ctx->tx_pwm_right = right_tele.out;
}

static void speedloop_sync_pid_cfg(void)
{
    VofaSpeedLoopCtx_t* ctx = VofaRegister_GetSpeedLoopCtx();

    Pid_SetGains(&s_left_pid, ctx->cmd_left_kp, ctx->cmd_left_ki, ctx->cmd_left_kd);
    Pid_SetGains(&s_right_pid, ctx->cmd_right_kp, ctx->cmd_right_ki, ctx->cmd_right_kd);
}

/* ---- 公开 API ----------------------------------------------------------- */

void SpeedLoop_Init(void)
{
    /* 仅做一次性调试状态初始化，不在这里注册运行态 profile。 */
    Pid_Init(&s_left_pid, &s_pid_cfg);
    Pid_Init(&s_right_pid, &s_pid_cfg);
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
    Pid_Reset(&s_left_pid);
    Pid_Reset(&s_right_pid);
    Motor_BrakeAll();
    VofaRegister_EnterProfile(VOFA_PROFILE_SPEED_LOOP);
    speedloop_sync_pid_cfg();
    speedloop_refresh_telemetry();
}

void SpeedLoop_Exit(void)
{
    /* 退出时清零控制状态并主动刹车，避免离开测试页后继续输出。 */
    speedloop_clear_targets();
    Pid_Reset(&s_left_pid);
    Pid_Reset(&s_right_pid);
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

    /* 速度环核心：左右轮各自增量式 PID，m/s 进、±1000 PWM 出。 */
    left_out = Pid_UpdateIncremental(&s_left_pid,
                                     (float)ctx->cmd_target_left_mps,
                                     s_encoder_snapshot.speed_mps[ENCODER_LEFT]);
    right_out = Pid_UpdateIncremental(&s_right_pid,
                                      (float)ctx->cmd_target_right_mps,
                                      s_encoder_snapshot.speed_mps[ENCODER_RIGHT]);

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
