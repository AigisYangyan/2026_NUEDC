/**
 * @file    vofa_register.c
 * @brief   任务级 VOFA Profile 注册中枢实现
 *
 * 本文件集中维护当前工程的任务级 VOFA 参数上下文、遥测上下文与 profile 注册流程。
 *
 * 功能范围：
 * - 维护当前激活 profile 的全局状态
 * - 维护各任务 profile 的参数与遥测上下文实例
 * - 在进入任务时注册对应发送通道和接收命令
 * - 在退出任务时统一清空当前 profile
 *
 * 不负责的内容：
 * - VOFA 协议帧编码与串口收发
 * - 任务控制链路本身的业务逻辑
 * - 菜单和调度状态切换
 *
 * 实现说明：
 * 1. 本模块是任务层与 uart_vofa 驱动层之间的唯一收口点
 * 2. 每个任务 profile 仅持有“可调参数 + 关键遥测”这一最小集合
 * 3. 进入 profile 时固定执行：清空旧 profile -> 重置上下文 -> 注册发送/接收项
 * 4. 退出 profile 时固定执行：清空 active profile -> 清空驱动层 profile
 */

#include "app/scheduler/vofa_register.h"

#include <string.h>

#include "driver/uart_vofa/uart_vofa.h"

 /* ---- 默认参数 ----------------------------------------------------------- */
/* Emm 固件口径：
 * - Stepper / DEBUG_Smooth `cmd_speed*` 上层统一使用 RPM；协议发送前内部换算为 0.1RPM
 * - Stepper / DEBUG_Smooth `cmd_accel*` 使用 0~255 抽象加速度档位
 */

#define VOFA_STEPPER_DEFAULT_SPEED           30.0f   /* RPM */
#define VOFA_STEPPER_DEFAULT_ACCEL           0.0f    /* Emm acceleration grade */
#define VOFA_STEPPER_DEFAULT_OUTPUT_LIMIT    48.0f
#define VOFA_STEPPER_DEFAULT_INTEGRAL_LIMIT  1000.0f

/* ---- 模块状态 ----------------------------------------------------------- */

static VofaProfileId_e s_active_profile = VOFA_PROFILE_NONE; // 当前激活的 profile ID

static VofaSpeedLoopCtx_t s_speed_loop_ctx;// 速度环 profile 上下文实例
static VofaTask1Ctx_t s_task1_ctx;// TASK1 profile 上下文实例
static VofaGrayTestCtx_t s_gray_test_ctx;// 灰度测试 profile 上下文实例
static VofaUartTestCtx_t s_uart_test_ctx;// 串口测试 profile 上下文实例
static VofaVisionDataCtx_t s_vision_data_ctx;// 视觉数据 profile 上下文实例
static VofaDebugSmoothCtx_t s_debug_smooth_ctx;// DEBUG_Smooth profile 上下文实例
static VofaPidAxisCtx_t s_stepper_x_ctx;// 二维平台步进电机 X 轴 profile 上下文实例
static VofaPidAxisCtx_t s_stepper_y_ctx;// 二维平台步进电机 Y 轴 profile 上下文实例

/* ---- 静态重置函数 ------------------------------------------------------- */

/* 速度环/TASK1 重置：memset 前暂存当前 cmd 增益并恢复，
 * 保住「profile 重进不丢调参」的既有语义（原实现经 PID 全局回读，M01 后 PID 无全局）。 */
static void vofa_register_reset_speed_loop(void)
{
    float kp_l = s_speed_loop_ctx.cmd_left_kp;
    float ki_l = s_speed_loop_ctx.cmd_left_ki;
    float kd_l = s_speed_loop_ctx.cmd_left_kd;
    float kp_r = s_speed_loop_ctx.cmd_right_kp;
    float ki_r = s_speed_loop_ctx.cmd_right_ki;
    float kd_r = s_speed_loop_ctx.cmd_right_kd;

    memset(&s_speed_loop_ctx, 0, sizeof(s_speed_loop_ctx));
    s_speed_loop_ctx.cmd_left_kp = kp_l;
    s_speed_loop_ctx.cmd_left_ki = ki_l;
    s_speed_loop_ctx.cmd_left_kd = kd_l;
    s_speed_loop_ctx.cmd_right_kp = kp_r;
    s_speed_loop_ctx.cmd_right_ki = ki_r;
    s_speed_loop_ctx.cmd_right_kd = kd_r;
}

static void vofa_register_reset_task1(void)
{
    float kp_l = s_task1_ctx.cmd_left_kp;
    float ki_l = s_task1_ctx.cmd_left_ki;
    float kd_l = s_task1_ctx.cmd_left_kd;
    float kp_r = s_task1_ctx.cmd_right_kp;
    float ki_r = s_task1_ctx.cmd_right_ki;
    float kd_r = s_task1_ctx.cmd_right_kd;

    memset(&s_task1_ctx, 0, sizeof(s_task1_ctx));
    s_task1_ctx.cmd_left_kp = kp_l;
    s_task1_ctx.cmd_left_ki = ki_l;
    s_task1_ctx.cmd_left_kd = kd_l;
    s_task1_ctx.cmd_right_kp = kp_r;
    s_task1_ctx.cmd_right_ki = ki_r;
    s_task1_ctx.cmd_right_kd = kd_r;
}

static void vofa_register_reset_gray_test(void)
{
    memset(&s_gray_test_ctx, 0, sizeof(s_gray_test_ctx));
}

static void vofa_register_reset_uart_test(void)
{
    memset(&s_uart_test_ctx, 0, sizeof(s_uart_test_ctx));
}

static void vofa_register_reset_vision_data(void)
{
    memset(&s_vision_data_ctx, 0, sizeof(s_vision_data_ctx));
}

static void vofa_register_reset_debug_smooth(void)
{
    memset(&s_debug_smooth_ctx, 0, sizeof(s_debug_smooth_ctx));
    s_debug_smooth_ctx.cmd_axis = 1.0f;
    s_debug_smooth_ctx.cmd_mode = 0.0f;
    s_debug_smooth_ctx.cmd_run = 0.0f;
    s_debug_smooth_ctx.cmd_dir = 1.0f;
    s_debug_smooth_ctx.cmd_speed_raw = 1000.0f;
    s_debug_smooth_ctx.cmd_accel_raw = 100.0f;
    s_debug_smooth_ctx.cmd_pid_kp = 18000.0f;
    s_debug_smooth_ctx.cmd_pid_ki = 10.0f;
    s_debug_smooth_ctx.cmd_pid_kd = 18000.0f;
    s_debug_smooth_ctx.cmd_pid_save = 0.0f;
    s_debug_smooth_ctx.cmd_pid_apply = 0.0f;
    s_debug_smooth_ctx.tx_axis = s_debug_smooth_ctx.cmd_axis;
    s_debug_smooth_ctx.tx_mode = s_debug_smooth_ctx.cmd_mode;
    s_debug_smooth_ctx.tx_run = s_debug_smooth_ctx.cmd_run;
    s_debug_smooth_ctx.tx_dir_cmd = s_debug_smooth_ctx.cmd_dir;
    s_debug_smooth_ctx.tx_v_cmd = 0.0f;
    s_debug_smooth_ctx.tx_v_fbk_raw = 0.0f;
    s_debug_smooth_ctx.tx_a_cmd = s_debug_smooth_ctx.cmd_accel_raw;
    s_debug_smooth_ctx.tx_pid_kp = s_debug_smooth_ctx.cmd_pid_kp;
    s_debug_smooth_ctx.tx_pid_ki = s_debug_smooth_ctx.cmd_pid_ki;
    s_debug_smooth_ctx.tx_pid_kd = s_debug_smooth_ctx.cmd_pid_kd;
    s_debug_smooth_ctx.tx_pid_save = s_debug_smooth_ctx.cmd_pid_save;
    s_debug_smooth_ctx.tx_stage_or_case = 0.0f;
    s_debug_smooth_ctx.tx_ack_or_err = 0.0f;
    s_debug_smooth_ctx.tx_error_count = 0.0f;
}

static void vofa_register_reset_stepper_axis(VofaPidAxisCtx_t* ctx, float polarity)
{
    if (ctx == (VofaPidAxisCtx_t*)0) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->cmd_integral_limit = VOFA_STEPPER_DEFAULT_INTEGRAL_LIMIT;
    ctx->cmd_output_limit = VOFA_STEPPER_DEFAULT_OUTPUT_LIMIT;
    ctx->cmd_speed = VOFA_STEPPER_DEFAULT_SPEED;
    ctx->cmd_accel = VOFA_STEPPER_DEFAULT_ACCEL;
    ctx->cmd_polarity = polarity;
}

/* ---- 静态 profile 注册函数 ---------------------------------------------- */

static void vofa_register_profile_speed_loop(void)
{
    (void)vofa_register_float(&s_speed_loop_ctx.tx_target_left_mps);
    (void)vofa_register_float(&s_speed_loop_ctx.tx_target_right_mps);
    (void)vofa_register_float(&s_speed_loop_ctx.tx_feedback_left_mps);
    (void)vofa_register_float(&s_speed_loop_ctx.tx_feedback_right_mps);
    (void)vofa_register_float(&s_speed_loop_ctx.tx_pwm_left);
    (void)vofa_register_float(&s_speed_loop_ctx.tx_pwm_right);

    (void)vofa_bind_cmd("LM", &s_speed_loop_ctx.cmd_target_left_mps);
    (void)vofa_bind_cmd("RM", &s_speed_loop_ctx.cmd_target_right_mps);
    (void)vofa_bind_cmd("LP", &s_speed_loop_ctx.cmd_left_kp);
    (void)vofa_bind_cmd("LI", &s_speed_loop_ctx.cmd_left_ki);
    (void)vofa_bind_cmd("LD", &s_speed_loop_ctx.cmd_left_kd);
    (void)vofa_bind_cmd("RP", &s_speed_loop_ctx.cmd_right_kp);
    (void)vofa_bind_cmd("RI", &s_speed_loop_ctx.cmd_right_ki);
    (void)vofa_bind_cmd("RD", &s_speed_loop_ctx.cmd_right_kd);
}

static void vofa_register_profile_task1(void)
{
    (void)vofa_register_float(&s_task1_ctx.tx_state);
    (void)vofa_register_float(&s_task1_ctx.tx_lap);
    (void)vofa_register_float(&s_task1_ctx.tx_flag);
    (void)vofa_register_float(&s_task1_ctx.tx_turn_deg);
    (void)vofa_register_float(&s_task1_ctx.tx_track_err);
    (void)vofa_register_float(&s_task1_ctx.tx_speed_l);
    (void)vofa_register_float(&s_task1_ctx.tx_speed_r);

    (void)vofa_bind_cmd("LP", &s_task1_ctx.cmd_left_kp);
    (void)vofa_bind_cmd("LI", &s_task1_ctx.cmd_left_ki);
    (void)vofa_bind_cmd("LD", &s_task1_ctx.cmd_left_kd);
    (void)vofa_bind_cmd("RP", &s_task1_ctx.cmd_right_kp);
    (void)vofa_bind_cmd("RI", &s_task1_ctx.cmd_right_ki);
    (void)vofa_bind_cmd("RD", &s_task1_ctx.cmd_right_kd);
}

static void vofa_register_profile_gray_test(void)
{
    uint32_t index = 0u;

    for (index = 0u; index < TRACK_SENSOR_COUNT; index++) {
        (void)vofa_register_int(&s_gray_test_ctx.tx_channels[index]);
    }

    (void)vofa_register_int(&s_gray_test_ctx.tx_bitmap);
}

static void vofa_register_profile_uart_test(void)
{
    (void)vofa_register_float((float*)&s_uart_test_ctx.cmd_u1);
    (void)vofa_register_float((float*)&s_uart_test_ctx.cmd_u2);
    (void)vofa_register_float((float*)&s_uart_test_ctx.cmd_u3);
    (void)vofa_register_float((float*)&s_uart_test_ctx.cmd_u4);

    (void)vofa_bind_cmd("U1", &s_uart_test_ctx.cmd_u1);
    (void)vofa_bind_cmd("U2", &s_uart_test_ctx.cmd_u2);
    (void)vofa_bind_cmd("U3", &s_uart_test_ctx.cmd_u3);
    (void)vofa_bind_cmd("U4", &s_uart_test_ctx.cmd_u4);
}

static void vofa_register_profile_vision_data(void)
{
    (void)vofa_register_float(&s_vision_data_ctx.tx_pixel_err_x);
    (void)vofa_register_float(&s_vision_data_ctx.tx_pixel_err_y);
    (void)vofa_register_float(&s_vision_data_ctx.tx_frame_dt_ms);
    (void)vofa_register_float(&s_vision_data_ctx.tx_status);
}

static void vofa_register_profile_debug_smooth(void)
{
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_mode);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_run);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_axis);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_dir_cmd);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_v_cmd);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_v_fbk);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_v_fbk_raw);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_a_cmd);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_pid_kp);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_pid_ki);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_pid_kd);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_pid_save);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_stage_or_case);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_ack_or_err);
    (void)vofa_register_float(&s_debug_smooth_ctx.tx_error_count);

    (void)vofa_bind_cmd("AX", &s_debug_smooth_ctx.cmd_axis);
    (void)vofa_bind_cmd("MODE", &s_debug_smooth_ctx.cmd_mode);
    (void)vofa_bind_cmd("RUN", &s_debug_smooth_ctx.cmd_run);
    (void)vofa_bind_cmd("DIR", &s_debug_smooth_ctx.cmd_dir);
    (void)vofa_bind_cmd("V", &s_debug_smooth_ctx.cmd_speed_raw);
    (void)vofa_bind_cmd("A", &s_debug_smooth_ctx.cmd_accel_raw);
    (void)vofa_bind_cmd("KP", &s_debug_smooth_ctx.cmd_pid_kp);
    (void)vofa_bind_cmd("KI", &s_debug_smooth_ctx.cmd_pid_ki);
    (void)vofa_bind_cmd("KD", &s_debug_smooth_ctx.cmd_pid_kd);
    (void)vofa_bind_cmd("SAVE", &s_debug_smooth_ctx.cmd_pid_save);
    (void)vofa_bind_cmd("PID_APPLY", &s_debug_smooth_ctx.cmd_pid_apply);
}

static void vofa_register_profile_stepper_axis(VofaPidAxisCtx_t* ctx)
{
    if (ctx == (VofaPidAxisCtx_t*)0) {
        return;
    }

    (void)vofa_register_float(&ctx->tx_err_px);
    (void)vofa_register_float(&ctx->tx_out_pulse);
    (void)vofa_register_float(&ctx->tx_kp);
    (void)vofa_register_float(&ctx->tx_ki);
    (void)vofa_register_float(&ctx->tx_kd);
    (void)vofa_register_float(&ctx->tx_p_term);
    (void)vofa_register_float(&ctx->tx_i_term);
    (void)vofa_register_float(&ctx->tx_d_term);
    (void)vofa_register_float(&ctx->tx_status);
    (void)vofa_register_float(&ctx->tx_frame_dt_ms);

    (void)vofa_bind_cmd("KP", &ctx->cmd_kp);
    (void)vofa_bind_cmd("KI", &ctx->cmd_ki);
    (void)vofa_bind_cmd("KD", &ctx->cmd_kd);
    (void)vofa_bind_cmd("IL", &ctx->cmd_integral_limit);
    (void)vofa_bind_cmd("OL", &ctx->cmd_output_limit);
    (void)vofa_bind_cmd("SP", &ctx->cmd_speed);
    (void)vofa_bind_cmd("AC", &ctx->cmd_accel);
    (void)vofa_bind_cmd("SG", &ctx->cmd_polarity);
}

/* ---- 公开 API：生命周期 ------------------------------------------------ */

void VofaRegister_Init(void)
{
    s_active_profile = VOFA_PROFILE_NONE;
    vofa_register_reset_speed_loop();
    vofa_register_reset_task1();
    vofa_register_reset_gray_test();
    vofa_register_reset_uart_test();
    vofa_register_reset_vision_data();
    vofa_register_reset_debug_smooth();
    vofa_register_reset_stepper_axis(&s_stepper_x_ctx, 1.0f);
    vofa_register_reset_stepper_axis(&s_stepper_y_ctx, 1.0f);
}

void VofaRegister_EnterProfile(VofaProfileId_e profile)
{
    vofa_clear_profile();
    s_active_profile = profile;

    switch (profile) {
    case VOFA_PROFILE_SPEED_LOOP:
        vofa_register_reset_speed_loop();
        vofa_register_profile_speed_loop();
        break;

    case VOFA_PROFILE_UART_TEST:
        vofa_register_reset_uart_test();
        vofa_register_profile_uart_test();
        break;

    case VOFA_PROFILE_GRAY_TEST:
        vofa_register_reset_gray_test();
        vofa_register_profile_gray_test();
        break;

    case VOFA_PROFILE_TASK1:
        vofa_register_reset_task1();
        vofa_register_profile_task1();
        break;

    case VOFA_PROFILE_DEBUG_VISION_DATA:
        vofa_register_reset_vision_data();
        vofa_register_profile_vision_data();
        break;

    case VOFA_PROFILE_DEBUG_SMOOTH:
        vofa_register_reset_debug_smooth();
        vofa_register_profile_debug_smooth();
        break;

    case VOFA_PROFILE_STEPPER_X:
        vofa_register_reset_stepper_axis(&s_stepper_x_ctx, 1.0f);
        vofa_register_profile_stepper_axis(&s_stepper_x_ctx);
        break;

    case VOFA_PROFILE_STEPPER_Y:
        vofa_register_reset_stepper_axis(&s_stepper_y_ctx, 1.0f);
        vofa_register_profile_stepper_axis(&s_stepper_y_ctx);
        break;

    case VOFA_PROFILE_NONE:
    default:
        s_active_profile = VOFA_PROFILE_NONE;
        break;
    }
}

void VofaRegister_ExitProfile(void)
{
    s_active_profile = VOFA_PROFILE_NONE;
    vofa_clear_profile();
}

VofaProfileId_e VofaRegister_GetActiveProfile(void)
{
    return s_active_profile;
}

/* ---- 公开 API：上下文访问 ---------------------------------------------- */

VofaSpeedLoopCtx_t* VofaRegister_GetSpeedLoopCtx(void)
{
    return &s_speed_loop_ctx;
}

VofaTask1Ctx_t* VofaRegister_GetTask1Ctx(void)
{
    return &s_task1_ctx;
}

VofaGrayTestCtx_t* VofaRegister_GetGrayTestCtx(void)
{
    return &s_gray_test_ctx;
}

VofaUartTestCtx_t* VofaRegister_GetUartTestCtx(void)
{
    return &s_uart_test_ctx;
}

VofaVisionDataCtx_t* VofaRegister_GetVisionDataCtx(void)
{
    return &s_vision_data_ctx;
}

VofaDebugSmoothCtx_t* VofaRegister_GetDebugSmoothCtx(void)
{
    return &s_debug_smooth_ctx;
}

VofaPidAxisCtx_t* VofaRegister_GetStepperXCtx(void)
{
    return &s_stepper_x_ctx;
}

VofaPidAxisCtx_t* VofaRegister_GetStepperYCtx(void)
{
    return &s_stepper_y_ctx;
}
