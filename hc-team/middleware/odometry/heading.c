/**
 * @file    heading.c
 * @brief   航向角去卷实现 —— 见 heading.h 契约与 Nyquist 假设。
 */
#include "middleware/odometry/heading.h"

#include <stddef.h>

void Heading_Reset(Heading_T *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->last_wrapped_deg = 0.0f;
    ctx->wrap_count = 0;
    ctx->seeded = false;
}

float Heading_Unwrap(Heading_T *ctx, float yaw_wrapped_deg)
{
    float delta;

    if (ctx == NULL) {
        return yaw_wrapped_deg;
    }

    /* 首样本：确立基准，圈数为 0，原值返回。 */
    if (!ctx->seeded) {
        ctx->last_wrapped_deg = yaw_wrapped_deg;
        ctx->wrap_count = 0;
        ctx->seeded = true;
        return yaw_wrapped_deg;
    }

    /* 跨界补偿：delta < -180 说明从 +180 侧越界到 -180 侧（CCW），补 +1 圈；
     * delta > 180 反之（CW），补 -1 圈。±180 恰好边界不算跨界（严格不等）。 */
    delta = yaw_wrapped_deg - ctx->last_wrapped_deg;
    if (delta < -180.0f) {
        ctx->wrap_count += 1;
    } else if (delta > 180.0f) {
        ctx->wrap_count -= 1;
    }

    ctx->last_wrapped_deg = yaw_wrapped_deg;
    return yaw_wrapped_deg + (float)ctx->wrap_count * 360.0f;
}
