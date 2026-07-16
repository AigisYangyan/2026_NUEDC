/**
 * @file    2DPlatform_LaserStrike.c
 * @brief   二维平台激光打击模块实现
 *
 * 本文件实现二维平台视觉跟踪、单轴 Stepper PID 调参、DEBUG_Vision_data 和 DEBUG_Smooth
 * 四类运行态逻辑。
 *
 * 功能范围：
 * - 根据视觉误差执行双轴相对位移跟踪
 * - 提供 Stepper_X / Stepper_Y 单轴 PID 调参链路
 * - 提供视觉数据流遥测 profile
 * - 提供单轴 EMM42 机械平顺性 / 固件 PID 调参测试 profile
 *
 * 不负责的内容：
 * - 视觉协议解析与缓存维护
 * - 步进总线的底层 UART 发送调度
 * - 菜单状态切换与运行项注册
 *
 * 实现说明：
 * 1. 视觉跟踪和单轴调参共用同一文件，但运行态语义分开
 * 2. 单轴 Stepper 调参只激活一个轴，另一轴保持静默
 * 3. 本文件只负责上层控制编排，VOFA profile 注册统一走 scheduler/vofa_register
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "2DPlatform_LaserStrike.h"

#include "app/scheduler/vofa_register.h"
#include "app/tasks/platform_2d/stepmotor_bus.h"
#include "driver/step_motor/emm42.h"
#include "driver/uart_vofa/uart_vofa.h"
#include "middleware/pid/pid.h"
#include "vision_coord.h"

#include <string.h>

 /* ---- 静态配置 ----------------------------------------------------------- */

#define VISIONHDL_CENTER_X                  320
#define VISIONHDL_CENTER_Y                  240
#define VISIONHDL_DEADBAND_PX               6
#define VISIONHDL_LIMIT_PULSES_Y            400
#define VISIONHDL_LIMIT_PULSES_X            800
#define VISIONHDL_TRACK_KP_X                0.15f
#define VISIONHDL_TRACK_KP_Y                0.15f
#define VISIONHDL_TRACK_MAX_STEP_PULSE      48
#define VISIONHDL_TRACK_SPEED               30u
#define VISIONHDL_TRACK_ACC                 0u
#define VISIONHDL_SIGN_X                    1
#define VISIONHDL_SIGN_Y                    1

#define DEBUG_SMOOTH_DEFAULT_SPEED_RAW        100u  /* Emm RPM */
#define DEBUG_SMOOTH_DEFAULT_ACCEL_RAW        100u  /* Emm acceleration grade */
#define DEBUG_SMOOTH_DEFAULT_PID_KP           18000u
#define DEBUG_SMOOTH_DEFAULT_PID_KI           10u
#define DEBUG_SMOOTH_DEFAULT_PID_KD           18000u
#define DEBUG_SMOOTH_BRAKE_RUN_HOLD_TICKS     200u  /* 1.0s @ 5ms */
#define DEBUG_SMOOTH_BRAKE_STOP_HOLD_TICKS    120u  /* 0.6s @ 5ms */
#define DEBUG_SMOOTH_REVERSAL_HOLD_TICKS      200u  /* 1.0s @ 5ms */
#define DEBUG_SMOOTH_RECONFIG_STOP_TICKS      20u   /* 100ms @ 5ms */
#define DEBUG_SMOOTH_CMD_REFRESH_TICKS        20u   /* 100ms @ 5ms，避免 5ms 重发挤占 0x35 查询带宽 */
/* ---- 内部类型定义 ------------------------------------------------------- */

typedef enum {
    DEBUG_SMOOTH_STATE_IDLE = 0,
    DEBUG_SMOOTH_STATE_BRAKE_RUN,
    DEBUG_SMOOTH_STATE_BRAKE_STOP,
    DEBUG_SMOOTH_STATE_MANUAL_HOLD,
    DEBUG_SMOOTH_STATE_REVERSAL_FWD,
    DEBUG_SMOOTH_STATE_REVERSAL_REV,
    DEBUG_SMOOTH_STATE_SAFE_STOP
} DebugSmooth_State_e;

typedef enum {
    STEPPER_TEST_MODE_NONE = 0,
    STEPPER_TEST_MODE_X,
    STEPPER_TEST_MODE_Y
} StepperTestMode_e;

typedef struct {
    PID_T pid;
    bool has_seen_target;
    bool has_meta;
    uint32_t last_seq;
    uint32_t last_tick_ms;
    int32_t pending_pulse;
} StepperAxisRuntime_t;

typedef struct {
    bool valid;
    uint8_t direction;
    uint16_t speed;
    uint8_t accel;
    uint16_t age_ticks;
} DebugSmooth_CommandCache_t;

/* ---- 模块状态 ----------------------------------------------------------- */

static int32_t s_pos_x_pulse = 0;
static int32_t s_pos_y_pulse = 0;
static bool s_has_seen_target = false;

static bool s_debug_vision_active = false;
static uint32_t s_debug_vision_last_seq = 0u;
static uint32_t s_debug_vision_last_tick_ms = 0u;
static bool s_debug_vision_has_meta = false;
static DebugSmooth_State_e s_debug_smooth_state = DEBUG_SMOOTH_STATE_IDLE;
static uint32_t s_debug_smooth_state_ticks = 0u;
static bool s_debug_smooth_active = false;
static uint8_t s_debug_smooth_axis_id = (uint8_t)EMM42_AXIS_Y;
static uint8_t s_debug_smooth_mode = 0u;
static bool s_debug_smooth_reconfig_pending = false;
static DebugSmooth_CommandCache_t s_debug_smooth_cmd_cache[3];

static StepperTestMode_e s_stepper_test_mode = STEPPER_TEST_MODE_NONE;
static StepperAxisRuntime_t s_stepper_x_runtime;
static StepperAxisRuntime_t s_stepper_y_runtime;

/* ---- 静态辅助函数：通用工具 -------------------------------------------- */

static int32_t visionhdl_abs_i32(int32_t value)
{
    return (value >= 0) ? value : -value;
}

static float visionhdl_abs_f32(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static int32_t visionhdl_step_from_error(int32_t error, float kp, int32_t max_step)
{
    float step_f = (float)visionhdl_abs_i32(error) * kp;

    if (step_f < 1.0f) {
        step_f = 1.0f;
    }

    if (step_f > (float)max_step) {
        step_f = (float)max_step;
    }

    return (int32_t)step_f;
}

static int32_t visionhdl_clamp_delta(int32_t current_pos, int32_t delta, int32_t limit_pulses)
{
    int32_t next_pos = current_pos + delta;

    if (next_pos > limit_pulses) {
        return limit_pulses - current_pos;
    }

    if (next_pos < -limit_pulses) {
        return (-limit_pulses) - current_pos;
    }

    return delta;
}

static void visionhdl_move_axes(int32_t delta_y, int32_t delta_x, uint16_t speed, uint8_t acceleration)
{
    int32_t limited_y = visionhdl_clamp_delta(s_pos_y_pulse, delta_y, VISIONHDL_LIMIT_PULSES_Y);
    int32_t limited_x = visionhdl_clamp_delta(s_pos_x_pulse, delta_x, VISIONHDL_LIMIT_PULSES_X);

    if (limited_y != 0) {
        Emm42_MoveRelative(EMM42_AXIS_Y, limited_y, speed, acceleration);
        s_pos_y_pulse += limited_y;
    }

    if (limited_x != 0) {
        Emm42_MoveRelative(EMM42_AXIS_X, limited_x, speed, acceleration);
        s_pos_x_pulse += limited_x;
    }
}

static void visionhdl_run_track(const VisionCoord_Coordinates_t* p_coord)
{
    int32_t err_x = 0;
    int32_t err_y = 0;
    int32_t delta_x = 0;
    int32_t delta_y = 0;

    if (p_coord == NULL) {
        return;
    }

    err_x = (int32_t)p_coord->x - VISIONHDL_CENTER_X;
    err_y = (int32_t)p_coord->y - VISIONHDL_CENTER_Y;

    if (visionhdl_abs_i32(err_x) > VISIONHDL_DEADBAND_PX) {
        delta_x = visionhdl_step_from_error(err_x, VISIONHDL_TRACK_KP_X, VISIONHDL_TRACK_MAX_STEP_PULSE);
        delta_x = (err_x >= 0) ? delta_x : -delta_x;
        delta_x *= VISIONHDL_SIGN_X;
    }

    if (visionhdl_abs_i32(err_y) > VISIONHDL_DEADBAND_PX) {
        delta_y = visionhdl_step_from_error(err_y, VISIONHDL_TRACK_KP_Y, VISIONHDL_TRACK_MAX_STEP_PULSE);
        delta_y = (err_y >= 0) ? delta_y : -delta_y;
        delta_y *= VISIONHDL_SIGN_Y;
    }

    visionhdl_move_axes(delta_y, delta_x, VISIONHDL_TRACK_SPEED, VISIONHDL_TRACK_ACC);
}

static void visionhdl_reset_pid_runtime(PID_T* pid)
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

/* ---- 静态辅助函数：DEBUG_Vision_data ----------------------------------- */

static void debug_vision_data_reset_runtime(void)
{
    VofaVisionDataCtx_t* ctx = VofaRegister_GetVisionDataCtx();

    ctx->tx_pixel_err_x = 0.0f;
    ctx->tx_pixel_err_y = 0.0f;
    ctx->tx_frame_dt_ms = 0.0f;
    ctx->tx_status = 0.0f;
    s_debug_vision_last_seq = 0u;
    s_debug_vision_last_tick_ms = 0u;
    s_debug_vision_has_meta = false;
}

static void debug_vision_data_update_channels(void)
{
    VisionCoord_FrameState_t frame_state;
    uint32_t update_tick_ms = 0u;
    uint32_t update_seq = 0u;
    VofaVisionDataCtx_t* ctx = VofaRegister_GetVisionDataCtx();

    if ((VisionCoord_GetState(&frame_state) != true) ||
        (VisionCoord_GetStateUpdateMeta(&update_tick_ms, &update_seq) != true)) {
        debug_vision_data_reset_runtime();
        return;
    }

    ctx->tx_status = (float)frame_state.status;

    if (frame_state.status == VISION_COORD_STATUS_TARGET) {
        ctx->tx_pixel_err_x = (float)((int32_t)frame_state.coord.x - (int32_t)VISIONHDL_CENTER_X);
        ctx->tx_pixel_err_y = (float)((int32_t)frame_state.coord.y - (int32_t)VISIONHDL_CENTER_Y);
    }
    else {
        ctx->tx_pixel_err_x = 0.0f;
        ctx->tx_pixel_err_y = 0.0f;
    }

    if (s_debug_vision_has_meta == false) {
        s_debug_vision_has_meta = true;
        s_debug_vision_last_seq = update_seq;
        s_debug_vision_last_tick_ms = update_tick_ms;
        ctx->tx_frame_dt_ms = 0.0f;
        return;
    }

    if (update_seq != s_debug_vision_last_seq) {
        ctx->tx_frame_dt_ms = (float)(update_tick_ms - s_debug_vision_last_tick_ms);
        s_debug_vision_last_seq = update_seq;
        s_debug_vision_last_tick_ms = update_tick_ms;
    }
}

/* ---- 静态辅助函数：DEBUG_Smooth ---------------------------------------- */

static uint8_t debug_smooth_clamp_axis_id(float axis_raw)
{
    if ((uint32_t)(axis_raw + 0.5f) == (uint32_t)EMM42_AXIS_X) {
        return (uint8_t)EMM42_AXIS_X;
    }

    return (uint8_t)EMM42_AXIS_Y;
}

static uint8_t debug_smooth_clamp_mode(float mode_raw)
{
    if (mode_raw < 0.5f) {
        return 0u;
    }

    if (mode_raw < 1.5f) {
        return 1u;
    }

    return 2u;
}

static bool debug_smooth_run_enabled(float run_raw)
{
    return (run_raw >= 0.5f) ? true : false;
}

static uint8_t debug_smooth_clamp_dir(float dir_raw)
{
    return (dir_raw >= 0.5f) ? EMM42_DIR_CW : EMM42_DIR_CCW;
}

static uint8_t debug_smooth_clamp_accel(float accel_raw)
{
    if (accel_raw < (float)EMM42_ACCEL_MIN_GRADE) {
        return EMM42_ACCEL_MIN_GRADE;
    }

    if (accel_raw > (float)EMM42_ACCEL_MAX_GRADE) {
        return EMM42_ACCEL_MAX_GRADE;
    }

    return (uint8_t)(accel_raw + 0.5f);
}

static uint16_t debug_smooth_clamp_speed(float speed_raw)
{
    if (speed_raw < (float)EMM42_SPEED_MIN_RPM) {
        return EMM42_SPEED_MIN_RPM;
    }

    if (speed_raw > (float)EMM42_SPEED_MAX_RPM) {
        return EMM42_SPEED_MAX_RPM;
    }

    return (uint16_t)(speed_raw + 0.5f);
}

static uint32_t debug_smooth_clamp_pid_u32(float pid_raw)
{
    if (pid_raw <= 0.0f) {
        return 0u;
    }

    if (pid_raw >= 4294967295.0f) {
        return UINT32_MAX;
    }

    return (uint32_t)(pid_raw + 0.5f);
}

static uint8_t debug_smooth_clamp_save(float save_raw)
{
    return (save_raw >= 0.5f) ? 1u : 0u;
}

static int32_t debug_smooth_signed_velocity(uint8_t direction, uint16_t speed)
{
    if (direction == EMM42_DIR_CCW) {
        return -((int32_t)speed);
    }

    return (int32_t)speed;
}

static void debug_smooth_reset_command_cache(void)
{
    memset(s_debug_smooth_cmd_cache, 0, sizeof(s_debug_smooth_cmd_cache));
}

static void debug_smooth_tick_command_cache(void)
{
    uint8_t axis_id = 0u;

    for (axis_id = (uint8_t)EMM42_AXIS_Y; axis_id <= (uint8_t)EMM42_AXIS_X; axis_id++) {
        if ((s_debug_smooth_cmd_cache[axis_id].valid == true) &&
            (s_debug_smooth_cmd_cache[axis_id].age_ticks < UINT16_MAX)) {
            s_debug_smooth_cmd_cache[axis_id].age_ticks++;
        }
    }
}

static void debug_smooth_submit_speed_command(uint8_t axis_id,
    uint8_t direction,
    uint16_t speed,
    uint8_t accel_raw)
{
    DebugSmooth_CommandCache_t* cache = NULL;
    bool need_send = false;

    if ((axis_id < (uint8_t)EMM42_AXIS_Y) || (axis_id > (uint8_t)EMM42_AXIS_X)) {
        return;
    }

    cache = &s_debug_smooth_cmd_cache[axis_id];
    if ((cache->valid == false) ||
        (cache->direction != direction) ||
        (cache->speed != speed) ||
        (cache->accel != accel_raw) ||
        (cache->age_ticks >= DEBUG_SMOOTH_CMD_REFRESH_TICKS)) {
        need_send = true;
    }

    if (need_send == true) {
        Emm42_SendSpeedCommand(axis_id, direction, speed, accel_raw);
        cache->valid = true;
        cache->direction = direction;
        cache->speed = speed;
        cache->accel = accel_raw;
        cache->age_ticks = 0u;
    }
}

// 将当前调试平顺性测试相关的输入命令和状态信息刷新到 VOFA profile 中，供上层监控和分析使用，确保在调试过程中能够实时观察到输入命令和状态变化的关系，辅助
static void debug_smooth_refresh_tx_profile(void)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();
    uint8_t direction = debug_smooth_clamp_dir(ctx->cmd_dir);

    ctx->tx_axis = (float)debug_smooth_clamp_axis_id(ctx->cmd_axis);
    ctx->tx_mode = (float)debug_smooth_clamp_mode(ctx->cmd_mode);
    ctx->tx_run = (debug_smooth_run_enabled(ctx->cmd_run) == true) ? 1.0f : 0.0f;
    ctx->tx_dir_cmd = (direction == EMM42_DIR_CW) ? 1.0f : 0.0f;
    ctx->tx_a_cmd = (float)debug_smooth_clamp_accel(ctx->cmd_accel_raw);
    ctx->tx_pid_kp = (float)debug_smooth_clamp_pid_u32(ctx->cmd_pid_kp);
    ctx->tx_pid_ki = (float)debug_smooth_clamp_pid_u32(ctx->cmd_pid_ki);
    ctx->tx_pid_kd = (float)debug_smooth_clamp_pid_u32(ctx->cmd_pid_kd);
    ctx->tx_pid_save = (float)debug_smooth_clamp_save(ctx->cmd_pid_save);
    ctx->tx_stage_or_case = (float)s_debug_smooth_state;
}

static void debug_smooth_send_speed_single_axis(uint8_t axis_id,
    uint8_t direction,
    uint16_t speed,
    uint8_t accel_raw)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();

    debug_smooth_submit_speed_command(axis_id, direction, speed, accel_raw);
    ctx->tx_axis = (float)axis_id;
    ctx->tx_dir_cmd = (direction == EMM42_DIR_CW) ? 1.0f : 0.0f;
    ctx->tx_v_cmd = (float)debug_smooth_signed_velocity(direction, speed);
    ctx->tx_a_cmd = (float)accel_raw;
}

// 在切换测试模式或调参模式时，先紧急停机并进入安全停机状态，等待数十毫秒确保机械完全停稳后再进行后续操作，避免直接切换导致的机械冲击或不受控状态
static void debug_smooth_send_stop_all(uint8_t accel_raw)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();

    debug_smooth_submit_speed_command((uint8_t)EMM42_AXIS_Y, EMM42_DIR_CW, 0u, accel_raw);
    debug_smooth_submit_speed_command((uint8_t)EMM42_AXIS_X, EMM42_DIR_CW, 0u, accel_raw);
    ctx->tx_v_cmd = 0.0f;
    ctx->tx_a_cmd = (float)accel_raw;
}

static void debug_smooth_begin_mode(uint8_t mode)
{
    switch (mode) {
    case 1u:
        s_debug_smooth_state = DEBUG_SMOOTH_STATE_MANUAL_HOLD;
        break;

    case 2u:
        s_debug_smooth_state = DEBUG_SMOOTH_STATE_REVERSAL_FWD;
        break;

    case 0u:
    default:
        s_debug_smooth_state = DEBUG_SMOOTH_STATE_BRAKE_RUN;
        break;
    }

    s_debug_smooth_state_ticks = 0u;
    s_debug_smooth_reconfig_pending = false;
}

static void debug_smooth_request_reconfig(uint8_t axis_id, uint8_t mode, uint8_t accel_raw)
{
    s_debug_smooth_axis_id = axis_id;
    s_debug_smooth_mode = mode;
    s_debug_smooth_reconfig_pending = true;
    s_debug_smooth_state = DEBUG_SMOOTH_STATE_SAFE_STOP;
    s_debug_smooth_state_ticks = 0u;
    debug_smooth_send_stop_all(accel_raw);
}

static void debug_smooth_apply_pid_if_requested(void)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();
    uint8_t axis_id = debug_smooth_clamp_axis_id(ctx->cmd_axis);

    if (ctx->cmd_pid_apply < 0.5f) {
        return;
    }

    Emm42_SendPidConfigCommand(axis_id,
        debug_smooth_clamp_save(ctx->cmd_pid_save),
        debug_smooth_clamp_pid_u32(ctx->cmd_pid_kp),
        debug_smooth_clamp_pid_u32(ctx->cmd_pid_ki),
        debug_smooth_clamp_pid_u32(ctx->cmd_pid_kd));
    ctx->cmd_pid_apply = 0.0f;
}

static void debug_smooth_reset_runtime(void)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();

    debug_smooth_reset_command_cache();
    s_debug_smooth_state = DEBUG_SMOOTH_STATE_IDLE;
    s_debug_smooth_state_ticks = 0u;
    s_debug_smooth_reconfig_pending = false;
    s_debug_smooth_axis_id = debug_smooth_clamp_axis_id(ctx->cmd_axis);
    s_debug_smooth_mode = debug_smooth_clamp_mode(ctx->cmd_mode);

    ctx->tx_mode = (float)s_debug_smooth_mode;
    ctx->tx_run = 0.0f;
    ctx->tx_axis = (float)s_debug_smooth_axis_id;
    ctx->tx_dir_cmd = (ctx->cmd_dir >= 0.5f) ? 1.0f : 0.0f;
    ctx->tx_v_cmd = 0.0f;
    ctx->tx_v_fbk = 0.0f;
    ctx->tx_v_fbk_raw = 0.0f;
    ctx->tx_a_cmd = (float)debug_smooth_clamp_accel(ctx->cmd_accel_raw);
    ctx->tx_pid_kp = (float)debug_smooth_clamp_pid_u32(ctx->cmd_pid_kp);
    ctx->tx_pid_ki = (float)debug_smooth_clamp_pid_u32(ctx->cmd_pid_ki);
    ctx->tx_pid_kd = (float)debug_smooth_clamp_pid_u32(ctx->cmd_pid_kd);
    ctx->tx_pid_save = (float)debug_smooth_clamp_save(ctx->cmd_pid_save);
    ctx->tx_stage_or_case = (float)DEBUG_SMOOTH_STATE_IDLE;
    ctx->tx_ack_or_err = 0.0f;
    ctx->tx_error_count = 0.0f;
}

/* ---- 静态辅助函数：Stepper 单轴调参 ------------------------------------ */

static void stepper_axis_runtime_reset(StepperAxisRuntime_t* runtime)
{
    if (runtime == (StepperAxisRuntime_t*)0) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    visionhdl_reset_pid_runtime(&runtime->pid);
}

static uint16_t stepper_test_clamp_speed(float speed_raw)
{
    if (speed_raw < (float)EMM42_SPEED_MIN_RPM) {
        return EMM42_SPEED_MIN_RPM;
    }

    if (speed_raw > (float)EMM42_SPEED_MAX_RPM) {
        return EMM42_SPEED_MAX_RPM;
    }

    return (uint16_t)(speed_raw + 0.5f);
}

static uint8_t stepper_test_clamp_accel(float accel_raw)
{
    if (accel_raw < (float)EMM42_ACCEL_MIN_GRADE) {
        return EMM42_ACCEL_MIN_GRADE;
    }

    if (accel_raw > (float)EMM42_ACCEL_MAX_GRADE) {
        return EMM42_ACCEL_MAX_GRADE;
    }

    return (uint8_t)(accel_raw + 0.5f);
}

static int32_t stepper_test_round_pulse(float pulse)
{
    if (pulse > 0.0f) {
        return (int32_t)(pulse + 0.5f);
    }

    if (pulse < 0.0f) {
        return (int32_t)(pulse - 0.5f);
    }

    return 0;
}

static VofaPidAxisCtx_t* stepper_test_get_ctx(StepperTestMode_e mode)
{
    if (mode == STEPPER_TEST_MODE_X) {
        return VofaRegister_GetStepperXCtx();
    }

    if (mode == STEPPER_TEST_MODE_Y) {
        return VofaRegister_GetStepperYCtx();
    }

    return (VofaPidAxisCtx_t*)0;
}

static StepperAxisRuntime_t* stepper_test_get_runtime(StepperTestMode_e mode)
{
    if (mode == STEPPER_TEST_MODE_X) {
        return &s_stepper_x_runtime;
    }

    if (mode == STEPPER_TEST_MODE_Y) {
        return &s_stepper_y_runtime;
    }

    return (StepperAxisRuntime_t*)0;
}

static Emm42_Axis_e stepper_test_get_axis(StepperTestMode_e mode)
{
    return (mode == STEPPER_TEST_MODE_Y) ? EMM42_AXIS_Y : EMM42_AXIS_X;
}

static float stepper_test_get_error_px(const VisionCoord_FrameState_t* frame_state, StepperTestMode_e mode)
{
    if (frame_state == (const VisionCoord_FrameState_t*)0) {
        return 0.0f;
    }

    if (mode == STEPPER_TEST_MODE_Y) {
        return (float)((int32_t)frame_state->coord.y - (int32_t)VISIONHDL_CENTER_Y);
    }

    return (float)((int32_t)frame_state->coord.x - (int32_t)VISIONHDL_CENTER_X);
}

static void stepper_test_clear_observation(VofaPidAxisCtx_t* ctx, StepperAxisRuntime_t* runtime)
{
    if ((ctx == (VofaPidAxisCtx_t*)0) || (runtime == (StepperAxisRuntime_t*)0)) {
        return;
    }

    runtime->pending_pulse = 0;
    visionhdl_reset_pid_runtime(&runtime->pid);
    ctx->tx_err_px = 0.0f;
    ctx->tx_out_pulse = 0.0f;
    ctx->tx_kp = ctx->cmd_kp;
    ctx->tx_ki = ctx->cmd_ki;
    ctx->tx_kd = ctx->cmd_kd;
    ctx->tx_p_term = 0.0f;
    ctx->tx_i_term = 0.0f;
    ctx->tx_d_term = 0.0f;
}

static void stepper_test_sync_pid_cfg(PID_T* pid, const VofaPidAxisCtx_t* ctx)
{
    if ((pid == (PID_T*)0) || (ctx == (const VofaPidAxisCtx_t*)0)) {
        return;
    }

    pid->kp = ctx->cmd_kp;
    pid->ki = ctx->cmd_ki;
    pid->kd = ctx->cmd_kd;
    pid->integral_limit = (ctx->cmd_integral_limit > 0.0f) ? ctx->cmd_integral_limit : 0.0f;
    pid->limit = visionhdl_abs_f32(ctx->cmd_output_limit);
    pid->d_filter_alpha = 1.0f;
}

static void stepper_test_update_meta(VofaPidAxisCtx_t* ctx,
    StepperAxisRuntime_t* runtime,
    uint32_t update_tick_ms,
    uint32_t update_seq)
{
    if ((ctx == (VofaPidAxisCtx_t*)0) || (runtime == (StepperAxisRuntime_t*)0)) {
        return;
    }

    if (runtime->has_meta == false) {
        runtime->has_meta = true;
        runtime->last_seq = update_seq;
        runtime->last_tick_ms = update_tick_ms;
        ctx->tx_frame_dt_ms = 0.0f;
        return;
    }

    if (update_seq != runtime->last_seq) {
        ctx->tx_frame_dt_ms = (float)(update_tick_ms - runtime->last_tick_ms);
        runtime->last_seq = update_seq;
        runtime->last_tick_ms = update_tick_ms;
    }
}

static void stepper_test_prepare_command(StepperTestMode_e mode)
{
    VisionCoord_FrameState_t frame_state;
    uint32_t update_tick_ms = 0u;
    uint32_t update_seq = 0u;
    VofaPidAxisCtx_t* ctx = stepper_test_get_ctx(mode);
    StepperAxisRuntime_t* runtime = stepper_test_get_runtime(mode);
    float error_px = 0.0f;
    float polarity = 1.0f;

    if ((ctx == (VofaPidAxisCtx_t*)0) || (runtime == (StepperAxisRuntime_t*)0)) {
        return;
    }

    if ((VisionCoord_GetState(&frame_state) != true) ||
        (VisionCoord_GetStateUpdateMeta(&update_tick_ms, &update_seq) != true)) {
        ctx->tx_status = 0.0f;
        ctx->tx_frame_dt_ms = 0.0f;
        runtime->has_meta = false;
        runtime->has_seen_target = false;
        stepper_test_clear_observation(ctx, runtime);
        return;
    }

    ctx->tx_status = (float)frame_state.status;
    stepper_test_update_meta(ctx, runtime, update_tick_ms, update_seq);

    if (frame_state.status == VISION_COORD_STATUS_TARGET) {
        runtime->has_seen_target = true;
    }
    else if ((frame_state.status == VISION_COORD_STATUS_LOST_TARGET) &&
        (runtime->has_seen_target == false)) {
        stepper_test_clear_observation(ctx, runtime);
        return;
    }
    else if (frame_state.status == VISION_COORD_STATUS_NONE) {
        runtime->has_seen_target = false;
        stepper_test_clear_observation(ctx, runtime);
        return;
    }

    polarity = (ctx->cmd_polarity == 0.0f) ? 1.0f : ctx->cmd_polarity;
    error_px = stepper_test_get_error_px(&frame_state, mode) * polarity;

    ctx->tx_err_px = error_px;
    ctx->tx_kp = ctx->cmd_kp;
    ctx->tx_ki = ctx->cmd_ki;
    ctx->tx_kd = ctx->cmd_kd;

    if (visionhdl_abs_f32(error_px) <= (float)VISIONHDL_DEADBAND_PX) {
        stepper_test_clear_observation(ctx, runtime);
        return;
    }

    stepper_test_sync_pid_cfg(&runtime->pid, ctx);
    runtime->pid.target = error_px;
    runtime->pid.current = 0.0f;
    pid_formula_positional(&runtime->pid);
    pid_out_limit(&runtime->pid);

    ctx->tx_out_pulse = runtime->pid.out;
    ctx->tx_p_term = runtime->pid.p_out;
    ctx->tx_i_term = runtime->pid.i_out;
    ctx->tx_d_term = runtime->pid.d_out;

    runtime->pending_pulse = stepper_test_round_pulse(runtime->pid.out);
    if ((runtime->pending_pulse == 0) && (visionhdl_abs_f32(error_px) > (float)VISIONHDL_DEADBAND_PX)) {
        runtime->pending_pulse = (error_px > 0.0f) ? 1 : -1;
    }
}

static void stepper_test_control_once(StepperTestMode_e mode)
{
    VofaPidAxisCtx_t* ctx = stepper_test_get_ctx(mode);
    StepperAxisRuntime_t* runtime = stepper_test_get_runtime(mode);
    Emm42_Axis_e axis = stepper_test_get_axis(mode);
    uint16_t speed = 0u;
    uint8_t accel = 0u;

    if ((ctx == (VofaPidAxisCtx_t*)0) || (runtime == (StepperAxisRuntime_t*)0)) {
        return;
    }

    if (runtime->pending_pulse == 0) {
        return;
    }

    speed = stepper_test_clamp_speed(ctx->cmd_speed);
    accel = stepper_test_clamp_accel(ctx->cmd_accel);
    Emm42_MoveRelative(axis, runtime->pending_pulse, speed, accel);
    runtime->pending_pulse = 0;
}

static void stepper_test_enter_common(StepperTestMode_e mode, VofaProfileId_e profile)
{
    // 进入通用测试模式
    StepperAxisRuntime_t* runtime = stepper_test_get_runtime(mode);// 获取对应轴的运行时数据结构
    VofaPidAxisCtx_t* ctx = stepper_test_get_ctx(mode);// 获取对应轴的 VOFA PID 上下文

    // 安全停机并重置状态，确保进入测试模式时机械处于可控状态，避免直接切换导致的机械冲击或不受控状态
    if ((runtime == (StepperAxisRuntime_t*)0) || (ctx == (VofaPidAxisCtx_t*)0)) {
        return;
    }

    //重置坐标
    stepper_axis_runtime_reset(runtime);
    s_stepper_test_mode = mode;
    VofaRegister_EnterProfile(profile);
    stepper_test_clear_observation(ctx, runtime);
    StepmotorBus_ResetDiagCounters();
    StepmotorBus_SetControlGate(true);
    Emm42_EnableAll();
}

static void stepper_test_exit_common(StepperTestMode_e mode)
{
    StepperAxisRuntime_t* runtime = stepper_test_get_runtime(mode);
    VofaPidAxisCtx_t* ctx = stepper_test_get_ctx(mode);

    if ((runtime == (StepperAxisRuntime_t*)0) || (ctx == (VofaPidAxisCtx_t*)0)) {
        return;
    }

    runtime->pending_pulse = 0;
    stepper_test_clear_observation(ctx, runtime);
    StepmotorBus_ClearControlFrames();
    StepmotorBus_SetControlGate(false);
    s_stepper_test_mode = STEPPER_TEST_MODE_NONE;
    VofaRegister_ExitProfile();
}

/* ---- 公开 API：视觉跟踪与单轴调参 -------------------------------------- */

void VisionHdl_Init(void)
{
    s_pos_x_pulse = 0;
    s_pos_y_pulse = 0;
    s_has_seen_target = false;
    s_stepper_test_mode = STEPPER_TEST_MODE_NONE;
    stepper_axis_runtime_reset(&s_stepper_x_runtime);
    stepper_axis_runtime_reset(&s_stepper_y_runtime);

    Emm42_EnableAll();
    Emm42_SetAllAxesZero();
}

void VisionHdl_Enter(void)
{
    s_has_seen_target = false;
}

void VisionHdl_Exit(void)
{
}

void VisionHdl_Run10ms(void)
{
    VisionCoord_FrameState_t coord_state;

    if (s_stepper_test_mode != STEPPER_TEST_MODE_NONE) {
        stepper_test_prepare_command(s_stepper_test_mode);
        return;
    }

    if (VisionCoord_GetState(&coord_state) != true) {
        return;
    }

    if (coord_state.status == VISION_COORD_STATUS_TARGET) {
        s_has_seen_target = true;
        visionhdl_run_track(&coord_state.coord);
        return;
    }

    if (coord_state.status == VISION_COORD_STATUS_LOST_TARGET) {
        if (s_has_seen_target == false) {
            return;
        }

        visionhdl_run_track(&coord_state.coord);
    }
}

void VisionHdl_Control5ms(void)
{
    if (s_stepper_test_mode == STEPPER_TEST_MODE_NONE) {
        return;
    }

    stepper_test_control_once(s_stepper_test_mode);
}

void VisionHdl_Telemetry10ms(void)
{
    if (s_stepper_test_mode == STEPPER_TEST_MODE_NONE) {
        return;
    }

    vofa_run();
}

void StepperTestX_Enter(void)
{
    stepper_test_enter_common(STEPPER_TEST_MODE_X, VOFA_PROFILE_STEPPER_X);
}

void StepperTestX_Exit(void)
{
    stepper_test_exit_common(STEPPER_TEST_MODE_X);
}

void StepperTestY_Enter(void)
{
    stepper_test_enter_common(STEPPER_TEST_MODE_Y, VOFA_PROFILE_STEPPER_Y);
}

void StepperTestY_Exit(void)
{
    stepper_test_exit_common(STEPPER_TEST_MODE_Y);
}

/* ---- 公开 API：DEBUG_Vision_data --------------------------------------- */

void DebugVisionData_Init(void)
{
    s_debug_vision_active = false;
    debug_vision_data_reset_runtime();
}

void DebugVisionData_Enter(void)
{
    DebugVisionData_Init();
    VofaRegister_EnterProfile(VOFA_PROFILE_DEBUG_VISION_DATA);
    s_debug_vision_active = true;
}

void DebugVisionData_Exit(void)
{
    s_debug_vision_active = false;
    VofaRegister_ExitProfile();
}

void DebugVisionData_Telemetry10ms(void)
{
    if (s_debug_vision_active == false) {
        return;
    }

    debug_vision_data_update_channels();
    vofa_run();
}

/* ---- 公开 API：DEBUG_Smooth -------------------------------------------- */

void DebugSmooth_Init(void)
{
    s_debug_smooth_active = false;
    debug_smooth_reset_runtime();
}

void DebugSmooth_Enter(void)
{
    DebugSmooth_Init();
    VofaRegister_EnterProfile(VOFA_PROFILE_DEBUG_SMOOTH);
    debug_smooth_reset_runtime();

    StepmotorBus_ResetDiagCounters();//
    StepmotorBus_SetControlGate(true);//
    s_debug_smooth_active = true;
    Emm42_EnableAll();
    debug_smooth_send_stop_all((uint8_t)DEBUG_SMOOTH_DEFAULT_ACCEL_RAW);
    debug_smooth_refresh_tx_profile();
}

void DebugSmooth_Exit(void)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();
    uint8_t accel_raw = debug_smooth_clamp_accel(ctx->cmd_accel_raw);

    s_debug_smooth_active = false;
    debug_smooth_send_stop_all(accel_raw);
    s_debug_smooth_state = DEBUG_SMOOTH_STATE_IDLE;
    s_debug_smooth_state_ticks = 0u;
    s_debug_smooth_reconfig_pending = false;
    /* 退出路径必须快速返回 UI，避免 K4 因等待总线冲刷而表现为“卡死”。 */
    StepmotorBus_Service5ms();
    StepmotorBus_ClearControlFrames();
    StepmotorBus_SetControlGate(false);
    VofaRegister_ExitProfile();
}

void DebugSmooth_Control5ms(void)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();
    uint8_t axis_id = 0u;
    uint8_t mode = 0u;
    uint8_t direction = 0u;
    uint8_t reverse_direction = 0u;
    bool run_enabled = false;
    uint16_t speed = 0u;
    uint8_t accel_raw = debug_smooth_clamp_accel(ctx->cmd_accel_raw);

    if (s_debug_smooth_active == false) {
        return;
    }

    debug_smooth_tick_command_cache();

    axis_id = debug_smooth_clamp_axis_id(ctx->cmd_axis);
    mode = debug_smooth_clamp_mode(ctx->cmd_mode);
    direction = debug_smooth_clamp_dir(ctx->cmd_dir);
    reverse_direction = (direction == EMM42_DIR_CW) ? EMM42_DIR_CCW : EMM42_DIR_CW;
    run_enabled = debug_smooth_run_enabled(ctx->cmd_run);
    speed = debug_smooth_clamp_speed(ctx->cmd_speed_raw);

    debug_smooth_refresh_tx_profile();
    debug_smooth_apply_pid_if_requested();

    if ((axis_id != s_debug_smooth_axis_id) ||
        (mode != s_debug_smooth_mode)) {
        debug_smooth_request_reconfig(axis_id, mode, accel_raw);
        debug_smooth_refresh_tx_profile();
        return;
    }

    if (run_enabled == false) {
        if (s_debug_smooth_state != DEBUG_SMOOTH_STATE_IDLE) {
            debug_smooth_send_stop_all(accel_raw);
        }

        s_debug_smooth_axis_id = axis_id;
        s_debug_smooth_mode = mode;
        s_debug_smooth_state = DEBUG_SMOOTH_STATE_IDLE;
        s_debug_smooth_state_ticks = 0u;
        s_debug_smooth_reconfig_pending = false;
        ctx->tx_v_cmd = 0.0f;
        debug_smooth_refresh_tx_profile();
        return;
    }

    if (s_debug_smooth_state == DEBUG_SMOOTH_STATE_IDLE) {
        s_debug_smooth_axis_id = axis_id;
        s_debug_smooth_mode = mode;
        debug_smooth_begin_mode(mode);
    }

    switch (s_debug_smooth_state) {
    case DEBUG_SMOOTH_STATE_BRAKE_RUN:
        debug_smooth_send_speed_single_axis(axis_id, direction, speed, accel_raw);
        if (s_debug_smooth_state_ticks < UINT32_MAX) {
            s_debug_smooth_state_ticks++;
        }
        if (s_debug_smooth_state_ticks >= DEBUG_SMOOTH_BRAKE_RUN_HOLD_TICKS) {
            s_debug_smooth_state = DEBUG_SMOOTH_STATE_BRAKE_STOP;
            s_debug_smooth_state_ticks = 0u;
        }
        break;

    case DEBUG_SMOOTH_STATE_BRAKE_STOP:
        debug_smooth_send_stop_all(accel_raw);
        if (s_debug_smooth_state_ticks < UINT32_MAX) {
            s_debug_smooth_state_ticks++;
        }
        if (s_debug_smooth_state_ticks >= DEBUG_SMOOTH_BRAKE_STOP_HOLD_TICKS) {
            s_debug_smooth_state = DEBUG_SMOOTH_STATE_BRAKE_RUN;
            s_debug_smooth_state_ticks = 0u;
        }
        break;

    case DEBUG_SMOOTH_STATE_MANUAL_HOLD:
        debug_smooth_send_speed_single_axis(axis_id, direction, speed, accel_raw);
        break;

    case DEBUG_SMOOTH_STATE_REVERSAL_FWD:
        debug_smooth_send_speed_single_axis(axis_id, direction, speed, accel_raw);
        if (s_debug_smooth_state_ticks < UINT32_MAX) {
            s_debug_smooth_state_ticks++;
        }
        if (s_debug_smooth_state_ticks >= DEBUG_SMOOTH_REVERSAL_HOLD_TICKS) {
            s_debug_smooth_state = DEBUG_SMOOTH_STATE_REVERSAL_REV;
            s_debug_smooth_state_ticks = 0u;
        }
        break;

    case DEBUG_SMOOTH_STATE_REVERSAL_REV:
        debug_smooth_send_speed_single_axis(axis_id, reverse_direction, speed, accel_raw);
        if (s_debug_smooth_state_ticks < UINT32_MAX) {
            s_debug_smooth_state_ticks++;
        }
        if (s_debug_smooth_state_ticks >= DEBUG_SMOOTH_REVERSAL_HOLD_TICKS) {
            s_debug_smooth_state = DEBUG_SMOOTH_STATE_REVERSAL_FWD;
            s_debug_smooth_state_ticks = 0u;
        }
        break;

    case DEBUG_SMOOTH_STATE_SAFE_STOP:
        debug_smooth_send_stop_all(accel_raw);
        if (s_debug_smooth_state_ticks < UINT32_MAX) {
            s_debug_smooth_state_ticks++;
        }
        if (s_debug_smooth_state_ticks >= DEBUG_SMOOTH_RECONFIG_STOP_TICKS) {
            if (s_debug_smooth_reconfig_pending == true) {
                debug_smooth_begin_mode(s_debug_smooth_mode);
            }
            else {
                s_debug_smooth_state = DEBUG_SMOOTH_STATE_IDLE;
                s_debug_smooth_state_ticks = 0u;
            }
        }
        break;

    case DEBUG_SMOOTH_STATE_IDLE:
    default:
        debug_smooth_send_stop_all(accel_raw);
        break;
    }

    debug_smooth_refresh_tx_profile();
}

void DebugSmooth_Telemetry10ms(void)
{
    VofaDebugSmoothCtx_t* ctx = VofaRegister_GetDebugSmoothCtx();
    uint32_t error_count = StepmotorBus_GetControlErrorCount();
    uint8_t query_axis = s_debug_smooth_axis_id;

    debug_smooth_refresh_tx_profile();
    ctx->tx_ack_or_err = (float)StepmotorBus_GetLastReturnCode();
    ctx->tx_error_count = (float)error_count;

    /* 默认开启：每 10ms 向当前 active 轴发一次 0x35 读速度查询，
     * 解析由 stepmotor_bus RX 路径自动完成；
     * - tx_v_fbk     = 当前显示口径 RPM
     * - tx_v_fbk_raw = 0x35 原始速度字段 */
    Emm42_SendReadSpeedCommand(query_axis);
    ctx->tx_v_fbk = (float)StepmotorBus_GetLastSpeedRpm(query_axis);
    ctx->tx_v_fbk_raw = (float)StepmotorBus_GetLastSpeedRaw(query_axis);

    vofa_run();
}
