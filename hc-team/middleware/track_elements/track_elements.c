/*******************************************************************************
 * @file    track_elements.c
 * @brief   循迹元素检测器实现——几何特征提取 + 逐检测器连续置信去毛刺
 *
 * 一拍流程：位图屏蔽高位 → 按 bit0_is_left 求 car 坐标下的
 * count / leftmost / rightmost / span / touch_left / touch_right → 对每个启用检测器
 * 求谓词，成立则 count 饱和自增至 confirm_ticks、否则清 0 → count≥confirm_ticks 置
 * confirmed；由未确认转确认者，其上升沿并入 just_confirmed_mask（PollEvents 清）。
 *
 * 位序：position 0=车左、11=车右。bit0_is_left 是唯一修正点的透传应用（同 track_error），
 * 本模块不新增第二个反转开关（§8.2）。
 *******************************************************************************/

#include "track_elements.h"

#include <stddef.h>   /* NULL */

/* 位图有效位掩码：仅低 TRACK_ELEMENTS_CHANNEL_COUNT 位参与判定 */
#define TRACK_ELEMENTS_BITMAP_MASK ((uint16_t)((1u << TRACK_ELEMENTS_CHANNEL_COUNT) - 1u))

/* 一拍从位图提取的几何特征。 */
typedef struct {
    uint32_t count;        /* 置位路数 */
    uint32_t span;         /* rightmost−leftmost+1（count==0 时为 0） */
    bool     touch_left;   /* 最左路(position 0)置位 */
    bool     touch_right;  /* 最右路(position TRACK_ELEMENTS_CHANNEL_COUNT-1)置位 */
} TrackElements_Feature_T;

/* 把深色位图归约为 car 坐标（车左→车右）下的几何特征。 */
static TrackElements_Feature_T extract_feature(uint16_t dark_bitmap, bool bit0_is_left)
{
    TrackElements_Feature_T f = {0u, 0u, false, false};
    uint16_t bitmap = (uint16_t)(dark_bitmap & TRACK_ELEMENTS_BITMAP_MASK);
    uint32_t leftmost = TRACK_ELEMENTS_CHANNEL_COUNT; /* 哨兵：无置位 */
    uint32_t rightmost = 0u;
    uint32_t bit;

    for (bit = 0u; bit < TRACK_ELEMENTS_CHANNEL_COUNT; bit++) {
        if ((bitmap & (uint16_t)(1u << bit)) != 0u) {
            uint32_t pos = bit0_is_left
                ? bit
                : ((TRACK_ELEMENTS_CHANNEL_COUNT - 1u) - bit);

            if (pos < leftmost) {
                leftmost = pos;
            }
            if (pos > rightmost) {
                rightmost = pos;
            }
            f.count++;
        }
    }

    if (f.count > 0u) {
        f.span = (rightmost - leftmost) + 1u;
        f.touch_left = (leftmost == 0u);
        f.touch_right = (rightmost == (TRACK_ELEMENTS_CHANNEL_COUNT - 1u));
    }
    return f;
}

/* 某几何类别在本拍是否成立。 */
static bool predicate_holds(TrackElement_Kind kind,
                            const TrackElements_Feature_T *f,
                            const TrackElements_Config_T *cfg)
{
    switch (kind) {
    case TRACK_ELEMENT_GAP:
        return (f->count == 0u);
    case TRACK_ELEMENT_FULL_BAR:
        return f->touch_left && f->touch_right && (f->count >= cfg->full_bar_min_count);
    case TRACK_ELEMENT_BRANCH_LEFT:
        return f->touch_left && !f->touch_right && (f->span >= cfg->branch_min_span);
    case TRACK_ELEMENT_BRANCH_RIGHT:
        return f->touch_right && !f->touch_left && (f->span >= cfg->branch_min_span);
    default:
        return false;
    }
}

void TrackElements_Init(TrackElements_Detector_T *det, const TrackElements_Config_T *cfg)
{
    uint32_t k;

    if ((det == NULL) || (cfg == NULL)) {
        return;
    }

    det->cfg = *cfg;
    if (det->cfg.confirm_ticks == 0u) {
        det->cfg.confirm_ticks = 1u;    /* 归一化：至少 1 拍确认 */
    }
    for (k = 0u; k < TRACK_ELEMENT_COUNT; k++) {
        det->count[k] = 0u;
    }
    det->confirmed_mask = 0u;
    det->just_confirmed_mask = 0u;
}

void TrackElements_Update(TrackElements_Detector_T *det, uint16_t dark_bitmap)
{
    TrackElements_Feature_T f;
    uint32_t k;

    if (det == NULL) {
        return;
    }

    f = extract_feature(dark_bitmap, det->cfg.bit0_is_left);

    for (k = 0u; k < TRACK_ELEMENT_COUNT; k++) {
        uint16_t bit = (uint16_t)(1u << k);
        bool was_confirmed = ((det->confirmed_mask & bit) != 0u);
        bool confirmed_now;

        if ((det->cfg.enable_mask & bit) == 0u) {
            /* 未启用：永不计数 / 确认 */
            det->count[k] = 0u;
            det->confirmed_mask &= (uint16_t)~bit;
            continue;
        }

        if (predicate_holds((TrackElement_Kind)k, &f, &det->cfg)) {
            if (det->count[k] < det->cfg.confirm_ticks) {
                det->count[k]++;        /* 饱和自增 */
            }
        } else {
            det->count[k] = 0u;         /* 连续性要求：一拍不成立即清 0 */
        }

        confirmed_now = (det->count[k] >= det->cfg.confirm_ticks);
        if (confirmed_now) {
            det->confirmed_mask |= bit;
            if (!was_confirmed) {
                det->just_confirmed_mask |= bit;   /* 上升沿累积，直至 PollEvents 清 */
            }
        } else {
            det->confirmed_mask &= (uint16_t)~bit;
        }
    }
}

uint16_t TrackElements_PollEvents(TrackElements_Detector_T *det)
{
    uint16_t events;

    if (det == NULL) {
        return 0u;
    }
    events = det->just_confirmed_mask;
    det->just_confirmed_mask = 0u;
    return events;
}

uint16_t TrackElements_GetConfirmed(const TrackElements_Detector_T *det)
{
    if (det == NULL) {
        return 0u;
    }
    return det->confirmed_mask;
}

uint8_t TrackElements_GetConfidence(const TrackElements_Detector_T *det, TrackElement_Kind kind)
{
    if ((det == NULL) || ((uint32_t)kind >= TRACK_ELEMENT_COUNT)) {
        return 0u;
    }
    return det->count[kind];
}
