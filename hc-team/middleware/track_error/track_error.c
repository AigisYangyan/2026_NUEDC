/*******************************************************************************
 * @file    track_error.c
 * @brief   循迹误差估计器实现——加权重心
 *
 * 算法：误差 = (Σ 置位通道 index / 置位数 − 中心 5.5) × pitch_mm。
 * index 按「车左→车右」；bit0_is_left=false 时 index = (路数−1) − bit 位。
 *******************************************************************************/

#include "track_error.h"

/* 位图有效位掩码：仅低 TRACK_ERROR_CHANNEL_COUNT 位参与量化 */
#define TRACK_ERROR_BITMAP_MASK ((uint16_t)((1u << TRACK_ERROR_CHANNEL_COUNT) - 1u))

/* 阵列中心的 index 坐标：(12-1)/2 */
#define TRACK_ERROR_CENTER_INDEX 5.5f

bool TrackError_FromDarkBitmap(const TrackError_Config_T *config,
                               uint16_t dark_bitmap,
                               float *out_error_mm)
{
    uint16_t bitmap = (uint16_t)(dark_bitmap & TRACK_ERROR_BITMAP_MASK);
    float index_sum = 0.0f;
    uint32_t dark_count = 0u;
    uint32_t bit;

    if (bitmap == 0u) {
        return false;
    }

    for (bit = 0u; bit < TRACK_ERROR_CHANNEL_COUNT; bit++) {
        if ((bitmap & (uint16_t)(1u << bit)) != 0u) {
            uint32_t index = config->bit0_is_left
                ? bit
                : ((TRACK_ERROR_CHANNEL_COUNT - 1u) - bit);

            index_sum += (float)index;
            dark_count++;
        }
    }

    *out_error_mm = ((index_sum / (float)dark_count) - TRACK_ERROR_CENTER_INDEX)
        * config->pitch_mm;
    return true;
}
