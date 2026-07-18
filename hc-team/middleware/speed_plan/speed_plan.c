/**
 * @file    speed_plan.c
 * @brief   速度规划器实现——|error| → 目标基速映射 + 有界斜坡（纯算法，状态调用者持有）。
 *
 * 数据链（§8.2 登记）：
 *   track_error.error_mm [误差 mm，调用者 fabsf]
 *   → SpeedPlan_Update(|error|, elapsed) [目标映射 + 斜坡]
 *   → current_mps [m/s，夹在 [min,straight]]
 *   → 调用者（S02b line_follow）合成 Chassis_SetTargetMps(base±diff)
 *
 * 唯一所有权：基速映射 + 斜坡状态 + 输出限幅。不采样、不量化误差、不碰 Chassis。
 */
#include "middleware/speed_plan/speed_plan.h"

#include <math.h>
#include <stddef.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void SpeedPlan_Init(SpeedPlan_T *sp, const SpeedPlan_Config_T *cfg)
{
    if ((sp == NULL) || (cfg == NULL)) {
        return; /* NULL 吸收：不断言、不改任何状态 */
    }
    sp->cfg = *cfg;
    sp->current_mps = cfg->min_speed_mps; /* 安全起步：从最低速斜坡上来 */
}

float SpeedPlan_Update(SpeedPlan_T *sp, float abs_error_mm, uint32_t elapsed_ms)
{
    float e;
    float straight;
    float min_speed;
    float target;
    float rate;
    float delta_cap;
    float diff;

    if (sp == NULL) {
        return 0.0f;
    }

    e = fabsf(abs_error_mm); /* 内部取绝对值：曲率是对称的，符号无关 */
    straight = sp->cfg.straight_speed_mps;
    min_speed = sp->cfg.min_speed_mps;

    /* ① 目标映射：curve_error_mm≤0 退化为不降速；否则线性插值到 min。 */
    if (sp->cfg.curve_error_mm <= 0.0f) {
        target = straight;
    } else {
        float frac = e / sp->cfg.curve_error_mm;
        if (frac > 1.0f) {
            frac = 1.0f;
        }
        target = straight + (frac * (min_speed - straight));
    }
    /* 夹到 [min,straight]：契约保证 min≤straight，普通夹紧兜住插值端点的浮点舍入。 */
    target = clampf(target, min_speed, straight);

    /* ② 有界斜坡：朝 target 步进，单拍不超过 rate×elapsed。 */
    rate = (target > sp->current_mps) ? sp->cfg.accel_mps_per_s
                                      : sp->cfg.decel_mps_per_s;
    delta_cap = rate * ((float)elapsed_ms / 1000.0f);
    diff = target - sp->current_mps;
    if (fabsf(diff) <= delta_cap) {
        sp->current_mps = target;      /* 到位（含 diff==0 / elapsed==0 且已到位） */
    } else if (diff > 0.0f) {
        sp->current_mps += delta_cap;  /* 提速一步 */
    } else {
        sp->current_mps -= delta_cap;  /* 降速一步 */
    }

    return sp->current_mps;
}

float SpeedPlan_GetSpeed(const SpeedPlan_T *sp)
{
    return (sp == NULL) ? 0.0f : sp->current_mps;
}

void SpeedPlan_Reset(SpeedPlan_T *sp)
{
    if (sp == NULL) {
        return;
    }
    sp->current_mps = sp->cfg.min_speed_mps;
}
