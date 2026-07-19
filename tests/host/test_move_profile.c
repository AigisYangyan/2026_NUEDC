/**
 * @file    test_move_profile.c
 * @brief   Host tests for the distance-parameterized trapezoidal move profile (M04, 契约 §27).
 *
 * 约定回顾：
 * - v = clamp(min(cruise, sqrt(start^2 + 2*accel*s_m), sqrt(2*decel*rem_m)), 0, cruise)
 *   其中 s_m = dist/1000、rem_m = (target-dist)/1000（mm→m 量纲对齐）。
 * - dist>=target 或 target<=0 或 cfg==NULL → 0；dist<0 视为 0。
 * - 输出恒落 [0, cruise]；起点 = start_mps；减速段随剩余距离 → 0。
 */
#include "middleware/move_profile/move_profile.h"

#include <math.h>
#include <stdio.h>

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_NEAR(actual, expected, eps) do { \
    double _a = (double)(actual); \
    double _e = (double)(expected); \
    if (fabs(_a - _e) > (double)(eps)) { \
        printf("FAIL: %s ~= %g, expected %g at %s:%d\n", #actual, _a, _e, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define EPS 1e-4f

/* 基准配置：匀速 1.0 m/s、起步 0.2 m/s、加速=减速 0.5 m/s^2。 */
static MoveProfile_Config_T base_cfg(void)
{
    MoveProfile_Config_T c;
    c.cruise_mps = 1.0f;
    c.start_mps  = 0.2f;
    c.accel_mps2 = 0.5f;
    c.decel_mps2 = 0.5f;
    return c;
}

static int test_null_and_degenerate(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    TEST_ASSERT_NEAR(MoveProfile_Speed(NULL, 100.0f, 2000.0f), 0.0f, EPS);   /* cfg NULL */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 100.0f, 0.0f), 0.0f, EPS);      /* target<=0 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 100.0f, -50.0f), 0.0f, EPS);    /* target<0 */
    printf("PASS: test_null_and_degenerate\n");
    return 0;
}

static int test_start_point_is_start_speed(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    /* s=0 → 加速段 = sqrt(start^2)=start=0.2；减速段 sqrt(2*0.5*2.0)=1.414；min=0.2 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 0.0f, 2000.0f), 0.2f, EPS);
    printf("PASS: test_start_point_is_start_speed\n");
    return 0;
}

static int test_accel_region(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    /* s=100mm=0.1m → sqrt(0.04 + 2*0.5*0.1)=sqrt(0.14)=0.374166 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 100.0f, 2000.0f), 0.374166f, EPS);
    printf("PASS: test_accel_region\n");
    return 0;
}

static int test_cruise_clamped(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    /* s=1000mm 中点：v_acc=sqrt(0.04+1.0)=1.0198、v_dec=sqrt(1.0)=1.0 → 夹到 cruise=1.0 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 1000.0f, 2000.0f), 1.0f, EPS);
    printf("PASS: test_cruise_clamped\n");
    return 0;
}

static int test_decel_region(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    /* s=1900mm，rem=100mm=0.1m → sqrt(2*0.5*0.1)=sqrt(0.1)=0.316228 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 1900.0f, 2000.0f), 0.316228f, EPS);
    printf("PASS: test_decel_region\n");
    return 0;
}

static int test_at_or_past_target_is_zero(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 2000.0f, 2000.0f), 0.0f, EPS);   /* 恰到位 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 2500.0f, 2000.0f), 0.0f, EPS);   /* 越界 */
    printf("PASS: test_at_or_past_target_is_zero\n");
    return 0;
}

static int test_negative_dist_as_zero(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    /* dist<0 视为 0 → 起步速 0.2 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, -50.0f, 2000.0f), 0.2f, EPS);
    printf("PASS: test_negative_dist_as_zero\n");
    return 0;
}

static int test_symmetric_accel_decel(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    cfg.start_mps = 0.0f;   /* 对称：start=0、accel=decel → v(s)=v(S-s) */
    float a = MoveProfile_Speed(&cfg, 500.0f, 2000.0f);
    float b = MoveProfile_Speed(&cfg, 1500.0f, 2000.0f);
    /* v(500)=min(1.0, sqrt(2*0.5*0.5)=0.7071, sqrt(2*0.5*1.5)=1.2247)=0.70711 */
    TEST_ASSERT_NEAR(a, 0.707107f, EPS);
    TEST_ASSERT_NEAR(b, 0.707107f, EPS);
    TEST_ASSERT_NEAR(a, b, EPS);
    printf("PASS: test_symmetric_accel_decel\n");
    return 0;
}

static int test_triangular_never_reaches_cruise(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    cfg.start_mps = 0.0f;   /* 短程 S=400mm：峰值在中点 sqrt(2*0.5*0.2)=0.4472 < cruise */
    float peak = MoveProfile_Speed(&cfg, 200.0f, 400.0f);
    TEST_ASSERT_NEAR(peak, 0.447214f, EPS);
    /* 全程扫描：任何点都 < 0.4473（never hits cruise 1.0） */
    int i;
    for (i = 0; i <= 400; i += 10) {
        float v = MoveProfile_Speed(&cfg, (float)i, 400.0f);
        TEST_ASSERT_TRUE(v <= 0.447214f + EPS);
        TEST_ASSERT_TRUE(v >= 0.0f);
    }
    printf("PASS: test_triangular_never_reaches_cruise\n");
    return 0;
}

static int test_output_bounded_sweep(void)
{
    MoveProfile_Config_T cfg = base_cfg();
    int i;
    /* 长程扫描：每点 0 <= v <= cruise，减速末端趋 0 */
    for (i = 0; i <= 3000; i += 25) {
        float v = MoveProfile_Speed(&cfg, (float)i, 3000.0f);
        TEST_ASSERT_TRUE(v >= 0.0f);
        TEST_ASSERT_TRUE(v <= 1.0f + EPS);
    }
    /* 逼近终点趋 0：rem=1mm → sqrt(2*0.5*0.001)=0.0316 */
    TEST_ASSERT_NEAR(MoveProfile_Speed(&cfg, 2999.0f, 3000.0f), 0.031623f, EPS);
    printf("PASS: test_output_bounded_sweep\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_null_and_degenerate();
    failures += test_start_point_is_start_speed();
    failures += test_accel_region();
    failures += test_cruise_clamped();
    failures += test_decel_region();
    failures += test_at_or_past_target_is_zero();
    failures += test_negative_dist_as_zero();
    failures += test_symmetric_accel_decel();
    failures += test_triangular_never_reaches_cruise();
    failures += test_output_bounded_sweep();

    if (failures == 0) {
        printf("\nAll move profile tests passed.\n");
        return 0;
    }

    printf("\n%d move profile test(s) failed.\n", failures);
    return 1;
}
