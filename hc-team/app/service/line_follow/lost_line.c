/**
 * @file    lost_line.c
 * @brief   丢线恢复策略实现——方向记忆 + 固定回退误差 + 有界超时。
 */
#include "app/service/line_follow/lost_line.h"

void LostLine_Init(LostLine_T *ctx, float recovery_error_mm, uint32_t timeout_ms)
{
    ctx->recovery_error_mm = recovery_error_mm;
    ctx->timeout_ms = timeout_ms;
    ctx->last_valid_error_mm = 0.0f;
    ctx->lost_elapsed_ms = 0u;
}

void LostLine_NoteValid(LostLine_T *ctx, float error_mm)
{
    ctx->last_valid_error_mm = error_mm;
    ctx->lost_elapsed_ms = 0u;
}

bool LostLine_Tick(LostLine_T *ctx, uint32_t elapsed_ms, float *out_error_mm)
{
    float direction = 0.0f;

    ctx->lost_elapsed_ms += elapsed_ms;
    if (ctx->lost_elapsed_ms >= ctx->timeout_ms) {
        return false;
    }

    if (ctx->last_valid_error_mm > 0.0f) {
        direction = 1.0f;
    } else if (ctx->last_valid_error_mm < 0.0f) {
        direction = -1.0f;
    }
    *out_error_mm = direction * ctx->recovery_error_mm;
    return true;
}
