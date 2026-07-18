/**
 * @file    test_odometry.c
 * @brief   Host tests for pose dead-reckoning (M01, odometry.c).
 *
 * 约定回顾（契约 §14）：
 * - 航向权威 = IMU 去卷航向 × heading_sign；前进距离 = (dL+dR)/2 × mm_per_pulse。
 * - 位姿沿航向空间积分：x += fwd·cosθ，y += fwd·sinθ。
 * - heading_valid=false → 保持上次航向续走；纯旋转（轮反向、净前进 0）位置不变。
 */
#include "middleware/odometry/odometry.h"

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

static const Odometry_Config_T k_cfg_unit = { .mm_per_pulse = 1.0f, .heading_sign = 1.0f };

/* Init/Reset 清位姿到原点。 */
static int test_init_and_reset_zero_pose(void)
{
    Odometry_T o;
    Odometry_Pose_T p;

    Odometry_Init(&o, &k_cfg_unit);
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 0.0f, 1e-6f);

    Odometry_Update(&o, 100, 100, 0.0f, true);
    Odometry_Reset(&o);
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 0.0f, 1e-6f);
    printf("PASS: test_init_and_reset_zero_pose\n");
    return 0;
}

/* θ=0 直行：x 增、y≈0。 */
static int test_straight_drive_heading_zero(void)
{
    Odometry_T o;
    Odometry_Pose_T p;

    Odometry_Init(&o, &k_cfg_unit);
    Odometry_Update(&o, 100, 100, 0.0f, true);  /* fwd=(100+100)/2*1=100 */
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 100.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 0.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 0.0f, 1e-4f);
    printf("PASS: test_straight_drive_heading_zero\n");
    return 0;
}

/* θ=90°：沿 +Y 前进。 */
static int test_drive_heading_90_moves_plus_y(void)
{
    Odometry_T o;
    Odometry_Pose_T p;

    Odometry_Init(&o, &k_cfg_unit);
    Odometry_Update(&o, 100, 100, 90.0f, true);
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 0.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 100.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 90.0f, 1e-4f);
    printf("PASS: test_drive_heading_90_moves_plus_y\n");
    return 0;
}

/* mm_per_pulse 尺度：距离随换算系数缩放。 */
static int test_mm_per_pulse_scales_distance(void)
{
    Odometry_T o;
    Odometry_Pose_T p;
    Odometry_Config_T cfg = { .mm_per_pulse = 2.0f, .heading_sign = 1.0f };

    Odometry_Init(&o, &cfg);
    Odometry_Update(&o, 100, 100, 0.0f, true);  /* fwd=100*2=200 */
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 200.0f, 1e-3f);
    printf("PASS: test_mm_per_pulse_scales_distance\n");
    return 0;
}

/* heading_sign=−1：翻转转向感（同 yaw 走反向 Y）。 */
static int test_heading_sign_flips_turn(void)
{
    Odometry_T o;
    Odometry_Pose_T p;
    Odometry_Config_T cfg = { .mm_per_pulse = 1.0f, .heading_sign = -1.0f };

    Odometry_Init(&o, &cfg);
    Odometry_Update(&o, 100, 100, 90.0f, true);  /* heading = 90*(-1) = -90 → 走 -Y */
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, -90.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, -100.0f, 1e-3f);
    printf("PASS: test_heading_sign_flips_turn\n");
    return 0;
}

/* heading_valid=false：保持上次航向续走，忽略本拍 yaw，无 NaN。 */
static int test_invalid_heading_holds_last(void)
{
    Odometry_T o;
    Odometry_Pose_T p;

    Odometry_Init(&o, &k_cfg_unit);
    Odometry_Update(&o, 100, 100, 90.0f, true);   /* heading=90, y=100 */
    Odometry_Update(&o, 100, 100, 0.0f, false);   /* 无效：保持 90，忽略传入 0 → 再走 +Y */
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 90.0f, 1e-4f);  /* 未被 0 覆盖 */
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 0.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 200.0f, 1e-3f);
    TEST_ASSERT_TRUE(isfinite(p.x_mm) && isfinite(p.y_mm));
    printf("PASS: test_invalid_heading_holds_last\n");
    return 0;
}

/* 负增量：倒退，x 减。 */
static int test_negative_delta_reverses(void)
{
    Odometry_T o;
    Odometry_Pose_T p;

    Odometry_Init(&o, &k_cfg_unit);
    Odometry_Update(&o, -100, -100, 0.0f, true);  /* fwd=-100 */
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, -100.0f, 1e-3f);
    printf("PASS: test_negative_delta_reverses\n");
    return 0;
}

/* 纯旋转（轮反向、净前进 0）：位置不变，航向随 IMU（免 track_width 的直接体现）。 */
static int test_pure_rotation_no_translation(void)
{
    Odometry_T o;
    Odometry_Pose_T p;

    Odometry_Init(&o, &k_cfg_unit);
    Odometry_Update(&o, 50, -50, 45.0f, true);  /* avg=0 → fwd=0 */
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 0.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 45.0f, 1e-4f);  /* 航向来自 IMU，非轮差分 */
    printf("PASS: test_pure_rotation_no_translation\n");
    return 0;
}

/* 跨 wrap 曲线路径：航向去卷期间位姿有限且被总路径长界定。 */
static int test_cross_wrap_curved_path_finite(void)
{
    Odometry_T o;
    Odometry_Pose_T p;
    /* yaw 序列跨 +180 界：170 → 179 → -179 → -170，每拍前进 10mm，共 4 拍 40mm */
    const float yaw_seq[4] = { 170.0f, 179.0f, -179.0f, -170.0f };
    int i;

    Odometry_Init(&o, &k_cfg_unit);
    for (i = 0; i < 4; i++) {
        Odometry_Update(&o, 10, 10, yaw_seq[i], true);
    }
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_TRUE(isfinite(p.x_mm) && isfinite(p.y_mm) && isfinite(p.heading_deg));
    TEST_ASSERT_TRUE(sqrtf(p.x_mm * p.x_mm + p.y_mm * p.y_mm) <= 40.0f + 1e-3f);
    /* 去卷后航向连续越过 +180：最后一拍 -170 → 连续角 190 */
    TEST_ASSERT_FLOAT_NEAR(p.heading_deg, 190.0f, 1e-3f);
    printf("PASS: test_cross_wrap_curved_path_finite\n");
    return 0;
}

/* NULL 安全：ctx/out 为 NULL 无副作用、Init(NULL cfg) 不崩且无位移。 */
static int test_getpose_and_ctx_null_safe(void)
{
    Odometry_T o;
    Odometry_Pose_T p = { 7.0f, 7.0f, 7.0f };

    Odometry_GetPose(NULL, &p);        /* 不写 p */
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 7.0f, 1e-6f);
    Odometry_GetPose(&o, NULL);        /* 不崩 */
    Odometry_Update(NULL, 1, 1, 0.0f, true); /* 不崩 */

    Odometry_Init(&o, NULL);           /* cfg 归零 → mm_per_pulse=0 → 无位移 */
    Odometry_Update(&o, 100, 100, 0.0f, true);
    Odometry_GetPose(&o, &p);
    TEST_ASSERT_FLOAT_NEAR(p.x_mm, 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(p.y_mm, 0.0f, 1e-6f);
    printf("PASS: test_getpose_and_ctx_null_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_and_reset_zero_pose();
    failures += test_straight_drive_heading_zero();
    failures += test_drive_heading_90_moves_plus_y();
    failures += test_mm_per_pulse_scales_distance();
    failures += test_heading_sign_flips_turn();
    failures += test_invalid_heading_holds_last();
    failures += test_negative_delta_reverses();
    failures += test_pure_rotation_no_translation();
    failures += test_cross_wrap_curved_path_finite();
    failures += test_getpose_and_ctx_null_safe();

    if (failures == 0) {
        printf("\nAll odometry tests passed.\n");
        return 0;
    }

    printf("\n%d odometry test(s) failed.\n", failures);
    return 1;
}
