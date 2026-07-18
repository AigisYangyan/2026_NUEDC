/**
 * @file    line_follow.c
 * @brief   循迹外环服务实现——采样触发 → 误差/元素/速度规划 → 外环 PID → 差速级联内环。
 *
 * 数据链（§8.2 登记）：
 *   Gray_ReadDarkBitmap [位图, 一次原子读, 唯一采样点]
 *   ├─ TrackElements_Update(位图) [几何元素置信/上升沿事件——并列消费，同一位图]
 *   └─ TrackError_FromDarkBitmap(pitch_mm, bit0_is_left) [误差 mm，+ = 线在车右]
 *      →（丢线时 LostLine_Tick 给回退误差）
 *      ├─ SpeedPlan_Update(|error|, elapsed) → base [规划基速，曲率调制——基速调制唯一所有者]
 *      └─ Pid_UpdatePositional(误差, 0) → diff [差速修正 c, m/s, |c| ≤ diff_limit]
 *      → Chassis_SetTargetMps(base + c, base − c) [唯一合成点] → Chassis_Update()（内环）
 *
 * 状态机转移表：
 *   IDLE       --Start(配置有效)---------------> TRACKING（PID/丢线/速度规划/元素检测复位）
 *   TRACKING   --位图≠0-----------------------> TRACKING（记方向、清丢线计时）
 *   TRACKING   --位图=0-----------------------> RECOVERING（回退误差继续转向）
 *   TRACKING   --位图=0 且单拍即累计≥timeout--> LOST（timeout 配得小于拍长时的直达路径）
 *   RECOVERING --位图≠0-----------------------> TRACKING
 *   RECOVERING --累计丢线≥lost_timeout_ms-----> LOST（Chassis_Stop，静默）
 *   任意态     --Stop()------------------------> IDLE（Chassis_Stop）
 *   LOST       --仅 Stop/Start 可离开
 *   IDLE/LOST 完全静默：不采样、不发目标、不推进内环、不产生元素事件——刹车真值表得以保持
 *   （方案 b，契约修订 1）。全黑（十字）重心≈0 属 TRACKING 正常直行；语义识别归上层 Task。
 */
#include "app/service/line_follow/line_follow.h"

#include "app/service/chassis/chassis.h"
#include "app/service/line_follow/lost_line.h"
#include "driver/clock/clock.h"
#include "driver/gray/gray.h"
#include "middleware/pid/pid.h"
#include "middleware/speed_plan/speed_plan.h"
#include "middleware/track_elements/track_elements.h"
#include "middleware/track_error/track_error.h"

#include <math.h>

/* 外环控制周期：与内环同拍（10ms），沿用旧循迹环口径 */
#define LINE_FOLLOW_CONTROL_PERIOD_MS 10u

/* 灰度路数与误差量化路数的一致性由 Service 负责（track_error.h 契约注释指名） */
#if GRAY_CHANNEL_COUNT != TRACK_ERROR_CHANNEL_COUNT
#error "gray/track_error channel count mismatch: line_follow wiring is invalid"
#endif
/* 灰度路数与元素检测路数一致性（track_elements 自持常数，一致性由本服务负责） */
#if GRAY_CHANNEL_COUNT != TRACK_ELEMENTS_CHANNEL_COUNT
#error "gray/track_elements channel count mismatch: line_follow wiring is invalid"
#endif

/* 公共元素位与 track_elements 位序一致性（头不暴露 Middleware 枚举，此处编译期锁定，C99）。 */
typedef char line_follow_elem_bits_match[
    (LINE_FOLLOW_ELEM_GAP          == (1u << TRACK_ELEMENT_GAP)) &&
    (LINE_FOLLOW_ELEM_FULL_BAR     == (1u << TRACK_ELEMENT_FULL_BAR)) &&
    (LINE_FOLLOW_ELEM_BRANCH_LEFT  == (1u << TRACK_ELEMENT_BRANCH_LEFT)) &&
    (LINE_FOLLOW_ELEM_BRANCH_RIGHT == (1u << TRACK_ELEMENT_BRANCH_RIGHT)) ? 1 : -1];

/* 外环积分限幅（误差口径，mm·拍）：积分累计的是 mm 误差，不能沿用 Pid 模块
 * 「out_limit×3.5」推导（那是内环 误差/输出 同尺度的约定，外环两侧差约两个
 * 数量级，会让积分一拍饱和、ki 退化为符号继电器）。预算 = 满偏误差 × 100 拍
 * （10ms 拍 = 1 秒满偏），远大于单拍误差保住积分器语义，同时有界防 windup。 */
#define LINE_FOLLOW_INTEGRAL_BUDGET_TICKS 100.0f

/* 服务私有运行状态 */
static LineFollow_Config_T s_cfg;
static bool s_cfg_valid;
static LineFollow_State s_state;
static Pid_T s_outer_pid;
static LostLine_T s_lost;
static SpeedPlan_T s_speed_plan;
static TrackElements_Detector_T s_elements;
static uint32_t s_period_base_ms;
static uint16_t s_last_bitmap;
static float s_last_error_mm;
static float s_last_base_mps;
static float s_last_diff_mps;

/* 从服务 flat 配置组装速度规划配置（映射，不额外语义） */
static void build_speed_cfg(SpeedPlan_Config_T *out)
{
    out->straight_speed_mps = s_cfg.straight_speed_mps;
    out->min_speed_mps = s_cfg.min_speed_mps;
    out->curve_error_mm = s_cfg.curve_error_mm;
    out->accel_mps_per_s = s_cfg.accel_mps_per_s;
    out->decel_mps_per_s = s_cfg.decel_mps_per_s;
}

/* 从服务 flat 配置组装元素检测配置（bit0_is_left 与 track_error 共用同一字段） */
static void build_elements_cfg(TrackElements_Config_T *out)
{
    out->bit0_is_left = s_cfg.bit0_is_left;
    out->full_bar_min_count = s_cfg.full_bar_min_count;
    out->branch_min_span = s_cfg.branch_min_span;
    out->confirm_ticks = s_cfg.element_confirm_ticks;
    out->enable_mask = s_cfg.element_enable_mask;
}

/* 应用一拍外环输出：误差 → 差速修正，与规划基速合成底盘目标（唯一合成点） */
static void line_follow_apply(uint16_t bitmap, float error_mm, float base_speed_mps)
{
    float diff = Pid_UpdatePositional(&s_outer_pid, error_mm, 0.0f);

    Chassis_SetTargetMps(base_speed_mps + diff,
                         base_speed_mps - diff);
    s_last_bitmap = bitmap;
    s_last_error_mm = error_mm;
    s_last_base_mps = base_speed_mps;
    s_last_diff_mps = diff;
}

void LineFollow_Init(const LineFollow_Config_T *config)
{
    float full_scale_error_mm =
        ((float)(TRACK_ERROR_CHANNEL_COUNT - 1u) / 2.0f) * config->pitch_mm;
    Pid_Config_T pid_cfg = {
        .kp = 0.0f, .ki = 0.0f, .kd = 0.0f,
        .out_limit = config->diff_limit_mps,
        .integral_limit = LINE_FOLLOW_INTEGRAL_BUDGET_TICKS * full_scale_error_mm,
        .d_filter_alpha = 1.0f,
    };
    SpeedPlan_Config_T sp_cfg;
    TrackElements_Config_T el_cfg;

    s_cfg = *config;
    s_cfg_valid = (config->pitch_mm > 0.0f) && (config->diff_limit_mps > 0.0f);
    Pid_Init(&s_outer_pid, &pid_cfg);
    LostLine_Init(&s_lost, s_cfg.recovery_error_mm, s_cfg.lost_timeout_ms);
    build_speed_cfg(&sp_cfg);
    SpeedPlan_Init(&s_speed_plan, &sp_cfg);
    build_elements_cfg(&el_cfg);
    TrackElements_Init(&s_elements, &el_cfg);
    s_state = LINE_FOLLOW_IDLE;
    s_period_base_ms = Clock_NowMs();
    s_last_bitmap = 0u;
    s_last_error_mm = 0.0f;
    s_last_base_mps = 0.0f;
    s_last_diff_mps = 0.0f;
}

void LineFollow_SetGains(float kp, float ki, float kd)
{
    Pid_SetGains(&s_outer_pid, kp, ki, kd);
}

bool LineFollow_Start(void)
{
    TrackElements_Config_T el_cfg;

    if (!s_cfg_valid) {
        return false;
    }
    Pid_Reset(&s_outer_pid);
    LostLine_Init(&s_lost, s_cfg.recovery_error_mm, s_cfg.lost_timeout_ms);
    SpeedPlan_Reset(&s_speed_plan);       /* 回 min_speed：安全起步 */
    build_elements_cfg(&el_cfg);
    TrackElements_Init(&s_elements, &el_cfg); /* 清陈旧置信/事件，避免跨 run 误触发 */
    s_period_base_ms = Clock_NowMs();
    s_state = LINE_FOLLOW_TRACKING;
    return true;
}

void LineFollow_Update(void)
{
    uint32_t now_ms = Clock_NowMs();
    uint32_t elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */

    /* IDLE/LOST 完全静默：不采样、不发目标、不推进内环、不驱动元素检测，
     * 让 Chassis_Stop 的刹车真值表保持（方案 b，契约修订 1）。 */
    if ((s_state != LINE_FOLLOW_TRACKING) && (s_state != LINE_FOLLOW_RECOVERING)) {
        return;
    }

    if (elapsed_ms >= LINE_FOLLOW_CONTROL_PERIOD_MS) {
        TrackError_Config_T tcfg = {
            .pitch_mm = s_cfg.pitch_mm,
            .bit0_is_left = s_cfg.bit0_is_left,
        };
        uint16_t bitmap;
        float error_mm = 0.0f;
        float base;

        s_period_base_ms = now_ms;
        bitmap = Gray_ReadDarkBitmap();

        /* 并列消费者：同一张位图喂元素检测，不新开第二采样点（§8.2） */
        TrackElements_Update(&s_elements, bitmap);

        if (TrackError_FromDarkBitmap(&tcfg, bitmap, &error_mm)) {
            s_state = LINE_FOLLOW_TRACKING;
            LostLine_NoteValid(&s_lost, error_mm);
        } else if (LostLine_Tick(&s_lost, elapsed_ms, &error_mm)) {
            s_state = LINE_FOLLOW_RECOVERING;
        } else {
            /* 恢复超时：刹停并静默（安全项，§8.1 反馈失效必须停止输出）；
             * 本拍起不再推进内环，刹车保持到 Stop/Start。 */
            s_state = LINE_FOLLOW_LOST;
            s_last_bitmap = bitmap;
            s_last_diff_mps = 0.0f;
            Chassis_Stop();
            return;
        }

        /* 基速调制唯一所有者：speed_plan 读误差幅值副本（不复算）→ 规划基速 */
        base = SpeedPlan_Update(&s_speed_plan, fabsf(error_mm), elapsed_ms);
        line_follow_apply(bitmap, error_mm, base);
    }

    Chassis_Update(); /* 多环级联：TRACKING/RECOVERING 期间推进内环（自带门控） */
}

void LineFollow_Stop(void)
{
    s_state = LINE_FOLLOW_IDLE;
    s_last_diff_mps = 0.0f;
    Chassis_Stop();
}

LineFollow_State LineFollow_GetState(void)
{
    return s_state;
}

void LineFollow_GetTelemetry(LineFollow_Telemetry_T *out)
{
    out->dark_bitmap = s_last_bitmap;
    out->error_mm = s_last_error_mm;
    out->base_speed_mps = s_last_base_mps;
    out->diff_cmd_mps = s_last_diff_mps;
    out->confirmed_elements = TrackElements_GetConfirmed(&s_elements);
    out->state = s_state;
}

uint16_t LineFollow_PollElementEvents(void)
{
    return TrackElements_PollEvents(&s_elements);
}
