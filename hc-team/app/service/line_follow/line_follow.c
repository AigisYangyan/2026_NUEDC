/**
 * @file    line_follow.c
 * @brief   循迹外环服务实现——采样触发 → 误差/丢线策略 → 外环 PID → 差速级联内环。
 *
 * 数据链（§8.2 登记）：
 *   Gray_ReadDarkBitmap [位图, 一次原子读]
 *   → TrackError_FromDarkBitmap(pitch_mm, bit0_is_left) [误差 mm，+ = 线在车右]
 *   →（丢线时 LostLine_Tick 给回退误差）
 *   → Pid_UpdatePositional(误差, 0) [差速修正 c, m/s, |c| ≤ diff_limit]
 *   → Chassis_SetTargetMps(base + c, base − c) → Chassis_Update()（内环）
 *
 * 状态机转移表：
 *   IDLE       --Start(配置有效)---------------> TRACKING（PID/丢线策略复位）
 *   TRACKING   --位图≠0-----------------------> TRACKING（记方向、清丢线计时）
 *   TRACKING   --位图=0-----------------------> RECOVERING（回退误差继续转向）
 *   TRACKING   --位图=0 且单拍即累计≥timeout--> LOST（timeout 配得小于拍长时的直达路径）
 *   RECOVERING --位图≠0-----------------------> TRACKING
 *   RECOVERING --累计丢线≥lost_timeout_ms-----> LOST（Chassis_Stop，静默）
 *   任意态     --Stop()------------------------> IDLE（Chassis_Stop）
 *   LOST       --仅 Stop/Start 可离开
 *   IDLE/LOST 完全静默：不采样、不发目标、不推进内环——刹车真值表得以保持（方案 b，
 *   契约修订 1）。全黑（十字）重心≈0 属 TRACKING 正常直行；特征识别归上层 Task。
 */
#include "app/service/line_follow/line_follow.h"

#include "app/service/chassis/chassis.h"
#include "app/service/line_follow/lost_line.h"
#include "driver/clock/clock.h"
#include "driver/gray/gray.h"
#include "middleware/pid/pid.h"
#include "middleware/track_error/track_error.h"

/* 外环控制周期：与内环同拍（10ms），沿用旧循迹环口径 */
#define LINE_FOLLOW_CONTROL_PERIOD_MS 10u

/* 灰度路数与误差量化路数的一致性由 Service 负责（track_error.h 契约注释指名） */
#if GRAY_CHANNEL_COUNT != TRACK_ERROR_CHANNEL_COUNT
#error "gray/track_error channel count mismatch: line_follow wiring is invalid"
#endif

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
static uint32_t s_period_base_ms;
static uint16_t s_last_bitmap;
static float s_last_error_mm;
static float s_last_diff_mps;

/* 应用一拍外环输出：误差 → 差速修正 → 底盘目标 */
static void line_follow_apply(uint16_t bitmap, float error_mm)
{
    float diff = Pid_UpdatePositional(&s_outer_pid, error_mm, 0.0f);

    Chassis_SetTargetMps(s_cfg.base_speed_mps + diff,
                         s_cfg.base_speed_mps - diff);
    s_last_bitmap = bitmap;
    s_last_error_mm = error_mm;
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

    s_cfg = *config;
    s_cfg_valid = (config->pitch_mm > 0.0f) && (config->diff_limit_mps > 0.0f);
    Pid_Init(&s_outer_pid, &pid_cfg);
    LostLine_Init(&s_lost, s_cfg.recovery_error_mm, s_cfg.lost_timeout_ms);
    s_state = LINE_FOLLOW_IDLE;
    s_period_base_ms = Clock_NowMs();
    s_last_bitmap = 0u;
    s_last_error_mm = 0.0f;
    s_last_diff_mps = 0.0f;
}

void LineFollow_SetGains(float kp, float ki, float kd)
{
    Pid_SetGains(&s_outer_pid, kp, ki, kd);
}

bool LineFollow_Start(void)
{
    if (!s_cfg_valid) {
        return false;
    }
    Pid_Reset(&s_outer_pid);
    LostLine_Init(&s_lost, s_cfg.recovery_error_mm, s_cfg.lost_timeout_ms);
    s_period_base_ms = Clock_NowMs();
    s_state = LINE_FOLLOW_TRACKING;
    return true;
}

void LineFollow_Update(void)
{
    uint32_t now_ms = Clock_NowMs();
    uint32_t elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */

    /* IDLE/LOST 完全静默：不采样、不发目标、不推进内环，
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

        s_period_base_ms = now_ms;
        bitmap = Gray_ReadDarkBitmap();

        if (TrackError_FromDarkBitmap(&tcfg, bitmap, &error_mm)) {
            s_state = LINE_FOLLOW_TRACKING;
            LostLine_NoteValid(&s_lost, error_mm);
            line_follow_apply(bitmap, error_mm);
        } else if (LostLine_Tick(&s_lost, elapsed_ms, &error_mm)) {
            s_state = LINE_FOLLOW_RECOVERING;
            line_follow_apply(bitmap, error_mm);
        } else {
            /* 恢复超时：刹停并静默（安全项，§8.1 反馈失效必须停止输出）；
             * 本拍起不再推进内环，刹车保持到 Stop/Start。 */
            s_state = LINE_FOLLOW_LOST;
            s_last_bitmap = bitmap;
            s_last_diff_mps = 0.0f;
            Chassis_Stop();
            return;
        }
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
    out->diff_cmd_mps = s_last_diff_mps;
    out->state = s_state;
}
