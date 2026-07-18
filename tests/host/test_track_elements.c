/**
 * @file    test_track_elements.c
 * @brief   Host tests for the track-element detector (M02, 契约 §16).
 *
 * 约定回顾：
 * - position 0=车左, 11=车右（bit0_is_left=true 时 position i = bit i）。
 * - 掩码位号 = 1u << kind：GAP=0x1, FULL_BAR=0x2, BRANCH_LEFT=0x4, BRANCH_RIGHT=0x8。
 * - 谓词：GAP(count==0) / FULL_BAR(touch_left&&touch_right&&count≥full_bar_min_count)
 *   / BRANCH_LEFT(touch_left&&!touch_right&&span≥branch_min_span) / BRANCH_RIGHT(镜像)。
 * - 置信：连续 confirm_ticks 拍谓词成立 → confirmed + 上升沿事件；某拍不成立即清 0。
 */
#include "middleware/track_elements/track_elements.h"

#include <stdio.h>

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_EQ_U(actual, expected) do { \
    if ((unsigned)(actual) != (unsigned)(expected)) { \
        printf("FAIL: %s == %u, expected %u at %s:%d\n", #actual, \
               (unsigned)(actual), (unsigned)(expected), __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define MASK(kind) ((uint16_t)(1u << (kind)))

/* 基准配置：全启用；FULL_BAR≥8 路，BRANCH 跨度≥6，连续 3 拍确认。 */
static TrackElements_Config_T base_cfg(void)
{
    TrackElements_Config_T cfg;
    cfg.bit0_is_left       = true;
    cfg.full_bar_min_count = 8u;
    cfg.branch_min_span    = 6u;
    cfg.confirm_ticks      = 3u;
    cfg.enable_mask        = 0x000Fu; /* GAP|FULL_BAR|BRANCH_LEFT|BRANCH_RIGHT */
    return cfg;
}

/* 连续喂 n 拍同一位图。 */
static void feed(TrackElements_Detector_T *det, uint16_t bitmap, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        TrackElements_Update(det, bitmap);
    }
}

static int test_init_clears(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det), 0u);
    TEST_ASSERT_EQ_U(TrackElements_PollEvents(&det), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_FULL_BAR), 0u);
    printf("PASS: test_init_clears\n");
    return 0;
}

static int test_gap_confirms_then_resets(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x000u, 3);   /* 空位图连续 3 拍 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_GAP), MASK(TRACK_ELEMENT_GAP));
    TEST_ASSERT_EQ_U(TrackElements_PollEvents(&det) & MASK(TRACK_ELEMENT_GAP), MASK(TRACK_ELEMENT_GAP));

    /* 非空位图 → GAP 计数清 0、电平落 */
    TrackElements_Update(&det, 0x060u); /* 窄线簇 bit5|bit6 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_GAP), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 0u);
    printf("PASS: test_gap_confirms_then_resets\n");
    return 0;
}

static int test_full_bar_confirms(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x0FFFu, 3);  /* 全 12 路：触双边、count=12≥8 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_FULL_BAR), MASK(TRACK_ELEMENT_FULL_BAR));
    /* 触双边 → BRANCH 谓词（要求恰触单边）为假 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_LEFT), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_RIGHT), 0u);
    printf("PASS: test_full_bar_confirms\n");
    return 0;
}

static int test_full_bar_sparse_not_confirmed(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    /* 触双边(bit0+bit11)但 count=3<8：既非 FULL_BAR（路数不足），亦非 BRANCH（触双边） */
    TrackElements_Init(&det, &cfg);
    feed(&det, (uint16_t)(0x001u | 0x800u | 0x020u), 5);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det), 0u);
    printf("PASS: test_full_bar_sparse_not_confirmed\n");
    return 0;
}

static int test_branch_left_confirms(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x07Fu, 3);   /* bit0..bit6：触左(pos0)、未触右(pos11)、span=7≥6 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_LEFT), MASK(TRACK_ELEMENT_BRANCH_LEFT));
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_RIGHT), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_FULL_BAR), 0u);
    printf("PASS: test_branch_left_confirms\n");
    return 0;
}

static int test_branch_right_confirms(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0xFE0u, 3);   /* bit5..bit11：触右(pos11)、未触左(pos0)、span=7≥6 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_RIGHT), MASK(TRACK_ELEMENT_BRANCH_RIGHT));
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_LEFT), 0u);
    printf("PASS: test_branch_right_confirms\n");
    return 0;
}

static int test_branch_narrow_not_confirmed(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    /* bit0..bit4：触左、span=5<6 → 边界下不判 BRANCH */
    TrackElements_Init(&det, &cfg);
    feed(&det, 0x01Fu, 5);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det), 0u);
    printf("PASS: test_branch_narrow_not_confirmed\n");
    return 0;
}

static int test_bit0_is_left_reversal(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    /* 唯一修正点反转：同一位图 0x07F 由 BRANCH_LEFT 变 BRANCH_RIGHT */
    cfg.bit0_is_left = false;   /* position i = 11 - bit i；bit0..6 → pos11..5 */
    TrackElements_Init(&det, &cfg);
    feed(&det, 0x07Fu, 3);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_RIGHT), MASK(TRACK_ELEMENT_BRANCH_RIGHT));
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_LEFT), 0u);
    printf("PASS: test_bit0_is_left_reversal\n");
    return 0;
}

static int test_debounce_needs_consecutive(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x000u, 2);   /* 只 2 拍（< confirm_ticks=3） */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 2u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_GAP), 0u);

    /* 单拍毛刺不确认：满 2 拍后插入一次非空，再满 1 拍 → 仍未确认 */
    TrackElements_Update(&det, 0x060u); /* 打断 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 0u);
    TrackElements_Update(&det, 0x000u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_GAP), 0u);
    printf("PASS: test_debounce_needs_consecutive\n");
    return 0;
}

static int test_miss_resets_count(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x07Fu, 2);   /* BRANCH_LEFT 累计 2 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_BRANCH_LEFT), 2u);
    TrackElements_Update(&det, 0x060u); /* miss → 清 0 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_BRANCH_LEFT), 0u);
    feed(&det, 0x07Fu, 2);   /* 重新累计仅 2 → 仍未确认 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_LEFT), 0u);
    printf("PASS: test_miss_resets_count\n");
    return 0;
}

static int test_poll_events_edge_once_level_persists(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x0FFFu, 3);  /* FULL_BAR 确认 */
    /* 首次 Poll 拿到上升沿事件 */
    TEST_ASSERT_EQ_U(TrackElements_PollEvents(&det) & MASK(TRACK_ELEMENT_FULL_BAR), MASK(TRACK_ELEMENT_FULL_BAR));
    /* 二次 Poll（无新升起）= 0，但电平仍在 */
    feed(&det, 0x0FFFu, 2);  /* 持续成立，不产生新上升沿（已确认，饱和） */
    TEST_ASSERT_EQ_U(TrackElements_PollEvents(&det), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_FULL_BAR), MASK(TRACK_ELEMENT_FULL_BAR));
    printf("PASS: test_poll_events_edge_once_level_persists\n");
    return 0;
}

static int test_enable_mask_gates_detector(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    cfg.enable_mask = (uint16_t)(0x000Fu & ~MASK(TRACK_ELEMENT_BRANCH_LEFT)); /* 关掉 BRANCH_LEFT */
    TrackElements_Init(&det, &cfg);
    feed(&det, 0x07Fu, 6);   /* 明确的 BRANCH_LEFT 形态，喂足够多拍 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_BRANCH_LEFT), 0u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_BRANCH_LEFT), 0u);
    printf("PASS: test_enable_mask_gates_detector\n");
    return 0;
}

static int test_high_bits_masked(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0xF000u, 3);  /* 仅高 4 位 == 空位图 == GAP */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_GAP), MASK(TRACK_ELEMENT_GAP));
    printf("PASS: test_high_bits_masked\n");
    return 0;
}

static int test_normal_narrow_line_no_false_positive(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    feed(&det, 0x060u, 8);   /* bit5|bit6 居中窄线：正常循迹，不触发任何元素 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det), 0u);
    TEST_ASSERT_EQ_U(TrackElements_PollEvents(&det), 0u);
    printf("PASS: test_normal_narrow_line_no_false_positive\n");
    return 0;
}

static int test_confirm_ticks_zero_normalized(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    cfg.confirm_ticks = 0u;  /* 归一化为 1：单拍即确认 */
    TrackElements_Init(&det, &cfg);
    TrackElements_Update(&det, 0x000u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfirmed(&det) & MASK(TRACK_ELEMENT_GAP), MASK(TRACK_ELEMENT_GAP));
    printf("PASS: test_confirm_ticks_zero_normalized\n");
    return 0;
}

static int test_get_confidence_reflects_count(void)
{
    TrackElements_Detector_T det;
    TrackElements_Config_T cfg = base_cfg();

    TrackElements_Init(&det, &cfg);
    TrackElements_Update(&det, 0x000u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 1u);
    TrackElements_Update(&det, 0x000u);
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 2u);
    feed(&det, 0x000u, 5);   /* 饱和于 confirm_ticks=3 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_GAP), 3u);
    /* 越界 kind 返回 0 */
    TEST_ASSERT_EQ_U(TrackElements_GetConfidence(&det, TRACK_ELEMENT_COUNT), 0u);
    printf("PASS: test_get_confidence_reflects_count\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_clears();
    failures += test_gap_confirms_then_resets();
    failures += test_full_bar_confirms();
    failures += test_full_bar_sparse_not_confirmed();
    failures += test_branch_left_confirms();
    failures += test_branch_right_confirms();
    failures += test_branch_narrow_not_confirmed();
    failures += test_bit0_is_left_reversal();
    failures += test_debounce_needs_consecutive();
    failures += test_miss_resets_count();
    failures += test_poll_events_edge_once_level_persists();
    failures += test_enable_mask_gates_detector();
    failures += test_high_bits_masked();
    failures += test_normal_narrow_line_no_false_positive();
    failures += test_confirm_ticks_zero_normalized();
    failures += test_get_confidence_reflects_count();

    if (failures == 0) {
        printf("\nAll track element tests passed.\n");
        return 0;
    }

    printf("\n%d track element test(s) failed.\n", failures);
    return 1;
}
