/**
 * @file    test_track_error.c
 * @brief   Host tests for the weighted-centroid track error estimator (M02).
 *
 * 约定回顾（契约 §6）：
 * - 坐标 = (index − 5.5) × pitch_mm，index 按「车左→车右」；+误差 = 线在车中心右侧
 * - bit0_is_left 是位序左右唯一修正点；丢线返回 false 且不写输出
 */
#include "middleware/track_error/track_error.h"

#include <math.h>
#include <stdio.h>

#define TEST_ASSERT_FLOAT_NEAR(actual, expected, epsilon) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (double)(actual), (double)(expected), (double)(epsilon), \
               __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static const TrackError_Config_T k_cfg_10mm_bit0_left = {
    .pitch_mm = 10.0f,
    .bit0_is_left = true,
};

static int test_single_bit_far_left(void)
{
    float error = 0.0f;

    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left, 0x0001u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, -55.0f, 1e-4f);
    printf("PASS: test_single_bit_far_left\n");
    return 0;
}

static int test_single_bit_far_right(void)
{
    float error = 0.0f;

    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left, 0x0800u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, 55.0f, 1e-4f);
    printf("PASS: test_single_bit_far_right\n");
    return 0;
}

static int test_center_pair_zero(void)
{
    float error = 99.0f;

    /* bit5+bit6 关于中心对称：重心 0（正对线中央，10mm 线宽两路压线，手册 p38 场景） */
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left,
                                               (uint16_t)(0x0020u | 0x0040u), &error));
    TEST_ASSERT_FLOAT_NEAR(error, 0.0f, 1e-4f);
    printf("PASS: test_center_pair_zero\n");
    return 0;
}

static int test_adjacent_pair_midpoint(void)
{
    float error = 0.0f;

    /* bit2+bit3：((-3.5)+(-2.5))/2 × 10 = -30 */
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left,
                                               (uint16_t)(0x0004u | 0x0008u), &error));
    TEST_ASSERT_FLOAT_NEAR(error, -30.0f, 1e-4f);
    printf("PASS: test_adjacent_pair_midpoint\n");
    return 0;
}

static int test_asymmetric_cluster(void)
{
    float error = 0.0f;

    /* bit0..bit2：mean(-5.5,-4.5,-3.5) × 10 = -45 */
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left, 0x0007u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, -45.0f, 1e-4f);
    printf("PASS: test_asymmetric_cluster\n");
    return 0;
}

static int test_lost_line_returns_false_and_preserves_output(void)
{
    float error = 123.0f;

    TEST_ASSERT_TRUE(!TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left, 0x0000u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, 123.0f, 1e-6f);   /* 丢线不写输出 */
    printf("PASS: test_lost_line_returns_false_and_preserves_output\n");
    return 0;
}

static int test_bit_order_reversed(void)
{
    TrackError_Config_T cfg = k_cfg_10mm_bit0_left;
    float error = 0.0f;

    cfg.bit0_is_left = false;   /* 唯一左右修正点生效：bit0 变最右 */
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&cfg, 0x0001u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, 55.0f, 1e-4f);
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&cfg, 0x0800u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, -55.0f, 1e-4f);
    printf("PASS: test_bit_order_reversed\n");
    return 0;
}

static int test_pitch_scales_error(void)
{
    TrackError_Config_T cfg = k_cfg_10mm_bit0_left;
    float error = 0.0f;

    cfg.pitch_mm = 20.0f;
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&cfg, 0x0001u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, -110.0f, 1e-4f);
    printf("PASS: test_pitch_scales_error\n");
    return 0;
}

static int test_high_bits_masked(void)
{
    float error = 123.0f;

    /* 仅高 4 位置位 == 空位图 == 丢线 */
    TEST_ASSERT_TRUE(!TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left, 0xF000u, &error));
    TEST_ASSERT_FLOAT_NEAR(error, 123.0f, 1e-6f);

    /* 高位垃圾不得影响有效位结果：bit5 单独 = (5-5.5)*10 = -5 */
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left,
                                               (uint16_t)(0xF000u | 0x0020u), &error));
    TEST_ASSERT_FLOAT_NEAR(error, -5.0f, 1e-4f);
    printf("PASS: test_high_bits_masked\n");
    return 0;
}

static int test_all_dark_centroid_zero(void)
{
    float error = 99.0f;

    /* 全黑（十字/停止线）：重心为 0 是固有性质，特征识别归调用者 */
    TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left, 0x0FFFu, &error));
    TEST_ASSERT_FLOAT_NEAR(error, 0.0f, 1e-4f);
    printf("PASS: test_all_dark_centroid_zero\n");
    return 0;
}

static int test_bound_within_half_span(void)
{
    uint32_t i;
    float error = 0.0f;

    /* 居中所限：任意单路置位 |误差| ≤ 5.5×pitch */
    for (i = 0u; i < TRACK_ERROR_CHANNEL_COUNT; i++) {
        TEST_ASSERT_TRUE(TrackError_FromDarkBitmap(&k_cfg_10mm_bit0_left,
                                                   (uint16_t)(1u << i), &error));
        TEST_ASSERT_TRUE(fabsf(error) <= 55.0f + 1e-4f);
    }
    printf("PASS: test_bound_within_half_span\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_single_bit_far_left();
    failures += test_single_bit_far_right();
    failures += test_center_pair_zero();
    failures += test_adjacent_pair_midpoint();
    failures += test_asymmetric_cluster();
    failures += test_lost_line_returns_false_and_preserves_output();
    failures += test_bit_order_reversed();
    failures += test_pitch_scales_error();
    failures += test_high_bits_masked();
    failures += test_all_dark_centroid_zero();
    failures += test_bound_within_half_span();

    if (failures == 0) {
        printf("\nAll track error tests passed.\n");
        return 0;
    }

    printf("\n%d track error test(s) failed.\n", failures);
    return 1;
}
