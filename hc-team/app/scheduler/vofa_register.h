/**
 * @file    vofa_register.h
 * @brief   任务级 VOFA Profile 注册中枢对外接口定义
 *
 * 本头文件统一定义任务级 VOFA profile 的上下文类型、profile 标识和生命周期接口。
 *
 * 功能范围：
 * - 定义各任务 profile 的参数上下文与关键遥测上下文
 * - 定义当前工程统一使用的 profile 标识枚举
 * - 提供 profile 进入、退出和上下文访问接口
 *
 * 不负责的内容：
 * - VOFA 协议打包、串口发送和接收解析
 * - 各任务的业务控制逻辑和状态机推进
 * - 菜单跳转与系统调度策略
 *
 * 设计约定：
 * 1. 系统一次只允许一个任务 profile 处于激活状态
 * 2. 进入任务时统一注册该任务 profile，退出任务时统一清空 profile
 * 3. 驱动层 uart_vofa 只负责协议容器；任务参数集中由本模块持有
 * 4. 对外仅暴露“可调参数 + 关键遥测”，不暴露无关临时变量
 *
 * 使用方式：
 * 1. 系统初始化时调用 `VofaRegister_Init()`
 * 2. 任务进入时调用 `VofaRegister_EnterProfile()`
 * 3. 任务运行中通过 `VofaRegister_Get*Ctx()` 读写本任务上下文
 * 4. 任务退出时调用 `VofaRegister_ExitProfile()`
 */

#ifndef APP_SCHEDULER_VOFA_REGISTER_H
#define APP_SCHEDULER_VOFA_REGISTER_H

#include "app/tasks/track_follow/track_follow.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- Profile 标识 ------------------------------------------------------- */

typedef enum {
    VOFA_PROFILE_NONE = 0,
    VOFA_PROFILE_SPEED_LOOP,
    VOFA_PROFILE_UART_TEST,
    VOFA_PROFILE_GRAY_TEST,
    VOFA_PROFILE_TASK1,
    VOFA_PROFILE_DEBUG_VISION_DATA,
    VOFA_PROFILE_DEBUG_SMOOTH,
    VOFA_PROFILE_STEPPER_X,
    VOFA_PROFILE_STEPPER_Y
} VofaProfileId_e;

/* ---- Profile 上下文类型 ------------------------------------------------- */

typedef struct {
    volatile float cmd_target_left_mps;
    volatile float cmd_target_right_mps;
    volatile float cmd_left_kp;
    volatile float cmd_left_ki;
    volatile float cmd_left_kd;
    volatile float cmd_right_kp;
    volatile float cmd_right_ki;
    volatile float cmd_right_kd;
    float tx_target_left_mps;
    float tx_target_right_mps;
    float tx_feedback_left_mps;
    float tx_feedback_right_mps;
    float tx_pwm_left;
    float tx_pwm_right;
} VofaSpeedLoopCtx_t;

typedef struct {
    float tx_state;
    float tx_lap;
    float tx_flag;
    float tx_turn_deg;
    float tx_track_err;
    float tx_speed_l;
    float tx_speed_r;
    volatile float cmd_left_kp;
    volatile float cmd_left_ki;
    volatile float cmd_left_kd;
    volatile float cmd_right_kp;
    volatile float cmd_right_ki;
    volatile float cmd_right_kd;
} VofaTask1Ctx_t;

typedef struct {
    int tx_channels[TRACK_SENSOR_COUNT];
    int tx_bitmap;
} VofaGrayTestCtx_t;

typedef struct {
    volatile float cmd_u1;
    volatile float cmd_u2;
    volatile float cmd_u3;
    volatile float cmd_u4;
} VofaUartTestCtx_t;

typedef struct {
    float tx_pixel_err_x;
    float tx_pixel_err_y;
    float tx_frame_dt_ms;
    float tx_status;
} VofaVisionDataCtx_t;

typedef struct {
    volatile float cmd_axis;       /* 目标轴，1=Y，2=X */
    volatile float cmd_mode;       /* 0=急停循环，1=手动保持，2=换向压力 */
    volatile float cmd_run;        /* 0=停止，1=运行 */
    volatile float cmd_dir;        /* 0=CCW，1=CW */
    volatile float cmd_speed_raw;  /* 上层输入单位 RPM；协议发送前内部换算为 0.1RPM，范围 0~100 */
    volatile float cmd_accel_raw;  /* Emm 固件加速度档位，范围 0~255 */
    volatile float cmd_pid_kp;     /* Emm 固件 PID.Kp，32 位整数口径 */
    volatile float cmd_pid_ki;     /* Emm 固件 PID.Ki，32 位整数口径 */
    volatile float cmd_pid_kd;     /* Emm 固件 PID.Kd，32 位整数口径 */
    volatile float cmd_pid_save;   /* 0=不保存，1=写入 Flash */
    volatile float cmd_pid_apply;  /* 写 1 触发一次 PID 配置下发 */
    float tx_mode;
    float tx_run;
    float tx_axis;
    float tx_dir_cmd;
    float tx_v_cmd;                /* 当前下发到 EMM42 的命令速度，单位 RPM，不是真实反馈 */
    float tx_v_fbk;                /* 0x35 实时速度反馈，按当前显示口径换算后的 RPM */
    float tx_v_fbk_raw;            /* 0x35 原始速度字段，高低字节直接拼接后的带符号值 */
    float tx_a_cmd;
    float tx_pid_kp;
    float tx_pid_ki;
    float tx_pid_kd;
    float tx_pid_save;
    float tx_stage_or_case;
    float tx_ack_or_err;
    float tx_error_count;
} VofaDebugSmoothCtx_t;

typedef struct {
    volatile float cmd_kp;
    volatile float cmd_ki;
    volatile float cmd_kd;
    volatile float cmd_integral_limit;
    volatile float cmd_output_limit;
    volatile float cmd_speed;          /* 上层输入单位 RPM；协议发送前内部换算为 0.1RPM，范围 0~100 */
    volatile float cmd_accel;          /* Emm 固件加速度档位，范围 0~255 */
    volatile float cmd_polarity;
    float tx_err_px;
    float tx_out_pulse;
    float tx_kp;
    float tx_ki;
    float tx_kd;
    float tx_p_term;
    float tx_i_term;
    float tx_d_term;
    float tx_status;
    float tx_frame_dt_ms;              /* 视觉帧更新周期，单位 ms */
} VofaPidAxisCtx_t;

/* ---- 生命周期接口 ------------------------------------------------------- */

void VofaRegister_Init(void);
void VofaRegister_EnterProfile(VofaProfileId_e profile);
void VofaRegister_ExitProfile(void);
VofaProfileId_e VofaRegister_GetActiveProfile(void);

/* ---- 上下文访问接口 ----------------------------------------------------- */

VofaSpeedLoopCtx_t* VofaRegister_GetSpeedLoopCtx(void);
VofaTask1Ctx_t* VofaRegister_GetTask1Ctx(void);
VofaGrayTestCtx_t* VofaRegister_GetGrayTestCtx(void);
VofaUartTestCtx_t* VofaRegister_GetUartTestCtx(void);
VofaVisionDataCtx_t* VofaRegister_GetVisionDataCtx(void);
VofaDebugSmoothCtx_t* VofaRegister_GetDebugSmoothCtx(void);
VofaPidAxisCtx_t* VofaRegister_GetStepperXCtx(void);
VofaPidAxisCtx_t* VofaRegister_GetStepperYCtx(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_VOFA_REGISTER_H */
