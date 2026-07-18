/**
 * @file    odometry.c
 * @brief   里程计位姿 dead-reckoning 实现 —— 见 odometry.h 契约。
 */
#include "middleware/odometry/odometry.h"

#include <math.h>
#include <stddef.h>

/* 度 → 弧度换算常数（供 cosf/sinf 使用）。 */
#define ODOMETRY_DEG_TO_RAD (3.14159265358979323846f / 180.0f)

void Odometry_Init(Odometry_T *ctx, const Odometry_Config_T *cfg)
{
    if (ctx == NULL) {
        return;
    }
    if (cfg != NULL) {
        ctx->cfg = *cfg;
    } else {
        ctx->cfg.mm_per_pulse = 0.0f;
        ctx->cfg.heading_sign = 0.0f;
    }
    Odometry_Reset(ctx);
}

void Odometry_Reset(Odometry_T *ctx)
{
    if (ctx == NULL) {
        return;
    }
    Heading_Reset(&ctx->heading);
    ctx->x_mm = 0.0f;
    ctx->y_mm = 0.0f;
    ctx->heading_deg = 0.0f;
}

void Odometry_Update(Odometry_T *ctx,
                     int32_t delta_left_pulses,
                     int32_t delta_right_pulses,
                     float yaw_wrapped_deg,
                     bool heading_valid)
{
    float fwd_mm;
    float theta_rad;

    if (ctx == NULL) {
        return;
    }

    /* ① 航向：仅在 IMU 有效时推进去卷并应用符号修正；无效则保持上次航向
     * （不喂无效样本进 unwrap，避免 gap 后基准漂移）。 */
    if (heading_valid) {
        ctx->heading_deg =
            Heading_Unwrap(&ctx->heading, yaw_wrapped_deg) * ctx->cfg.heading_sign;
    }

    /* ② 前进距离：双轮增量均值 × 脉冲→距离换算（唯一所有者 cfg.mm_per_pulse）。 */
    fwd_mm = ((float)delta_left_pulses + (float)delta_right_pulses)
             * 0.5f * ctx->cfg.mm_per_pulse;

    /* ③ 沿当前航向做空间积分位置。 */
    theta_rad = ctx->heading_deg * ODOMETRY_DEG_TO_RAD;
    ctx->x_mm += fwd_mm * cosf(theta_rad);
    ctx->y_mm += fwd_mm * sinf(theta_rad);
}

void Odometry_GetPose(const Odometry_T *ctx, Odometry_Pose_T *out)
{
    if (ctx == NULL || out == NULL) {
        return;
    }
    out->x_mm = ctx->x_mm;
    out->y_mm = ctx->y_mm;
    out->heading_deg = ctx->heading_deg;
}
