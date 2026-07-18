/**
 * @file    test_speed_plan.c
 * @brief   Host tests for the speed planner (M03, 契约 §17).
 *
 * 约定回顾：
 * - 输入 abs_error_mm = 横向误差幅值（曲率代理，mm）；内部取绝对值。
 * - 映射：|err|≤0 → straight_speed；|err|≥curve_error_mm → min_speed；之间线性插值，夹到 [min,straight]。
 * - 斜坡：Δcap = (target>current?accel:decel)×elapsed_ms/1000；|target−current|≤Δcap 直接到位，否则步进 Δcap。
 * - Init/Reset 后 current==min_speed；任意 Update 后 min≤current≤straight。
 */
#include "middleware/speed_plan/speed_plan.h"

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

/* 基准配置：直道 1.0 m/s、入弯 0.3 m/s、误差 100mm 达下限、加速 2.0、减速 4.0 (m/s per s)。
 * 以 elapsed=10ms 计：加速单拍 +0.02、减速单拍 −0.04。 */
static SpeedPlan_Config_T base_cfg(void)
{
    SpeedPlan_Config_T c;
    c.straight_speed_mps = 1.0f;
    c.min_speed_mps      = 0.3f;
    c.curve_error_mm     = 100.0f;
    c.accel_mps_per_s    = 2.0f;
    c.decel_mps_per_s    = 4.0f;
    return c;
}

/* 连续喂 n 拍同一 (err, elapsed)。 */
static void ramp(SpeedPlan_T *sp, float err, uint32_t el, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        (void)SpeedPlan_Update(sp, err, el);
    }
}

static int test_init_starts_at_min(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 0.3f, EPS);
    printf("PASS: test_init_starts_at_min\n");
    return 0;
}

static int test_straight_ramps_up(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    /* 直道一拍：0.3 + 0.02 = 0.32，受 accel 斜坡限制，远未到 straight */
    float v = SpeedPlan_Update(&sp, 0.0f, 10u);
    TEST_ASSERT_NEAR(v, 0.32f, EPS);
    TEST_ASSERT_TRUE(v < 1.0f);
    printf("PASS: test_straight_ramps_up\n");
    return 0;
}

static int test_straight_reaches_straight(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 0.0f, 10u, 40);   /* (1.0−0.3)/0.02 = 35 拍，40 拍足够到位且不过冲 */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 1.0f, EPS);
    printf("PASS: test_straight_reaches_straight\n");
    return 0;
}

static int test_curve_ramps_down(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 0.0f, 10u, 40);   /* 先到 straight=1.0 */
    /* 入弯一拍（err=200≥100）：1.0 − 0.04 = 0.96，受 decel 斜坡限制 */
    float v = SpeedPlan_Update(&sp, 200.0f, 10u);
    TEST_ASSERT_NEAR(v, 0.96f, EPS);
    printf("PASS: test_curve_ramps_down\n");
    return 0;
}

static int test_curve_reaches_min(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 0.0f, 10u, 40);       /* 到 straight */
    ramp(&sp, 200.0f, 10u, 25);     /* (1.0−0.3)/0.04 = 17.5 拍，25 拍足够到 min */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 0.3f, EPS);
    printf("PASS: test_curve_reaches_min\n");
    return 0;
}

static int test_interpolation_midpoint(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    /* err=50 = curve_error/2 → frac=0.5 → target = 1.0 + 0.5×(0.3−1.0) = 0.65 */
    ramp(&sp, 50.0f, 10u, 60);      /* 充分斜坡到稳态 */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 0.65f, EPS);
    printf("PASS: test_interpolation_midpoint\n");
    return 0;
}

static int test_accel_slew_rate(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    /* 单拍 elapsed=100ms，accel=2.0 → 增量恰 0.2：0.3 → 0.5（不瞬跳到 straight） */
    float v = SpeedPlan_Update(&sp, 0.0f, 100u);
    TEST_ASSERT_NEAR(v, 0.5f, EPS);
    printf("PASS: test_accel_slew_rate\n");
    return 0;
}

static int test_decel_slew_rate(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 0.0f, 10u, 40);       /* 到 straight=1.0 */
    /* 单拍 elapsed=100ms，decel=4.0 → 减量恰 0.4：1.0 → 0.6 */
    float v = SpeedPlan_Update(&sp, 200.0f, 100u);
    TEST_ASSERT_NEAR(v, 0.6f, EPS);
    printf("PASS: test_decel_slew_rate\n");
    return 0;
}

static int test_elapsed_zero_no_change(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    float v = SpeedPlan_Update(&sp, 0.0f, 0u);   /* Δcap=0 → 不变 */
    TEST_ASSERT_NEAR(v, 0.3f, EPS);
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 0.3f, EPS);
    printf("PASS: test_elapsed_zero_no_change\n");
    return 0;
}

static int test_large_elapsed_no_overshoot(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    /* 超大 elapsed：Δcap 远超 diff → 一拍到位但不过冲 target=straight */
    float v = SpeedPlan_Update(&sp, 0.0f, 100000u);
    TEST_ASSERT_NEAR(v, 1.0f, EPS);
    printf("PASS: test_large_elapsed_no_overshoot\n");
    return 0;
}

static int test_curve_error_zero_degenerate(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    cfg.curve_error_mm = 0.0f;      /* 退化：不做曲率降速，目标恒 straight */
    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 500.0f, 10u, 40);     /* 大误差但退化 → 仍朝 straight 提速 */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 1.0f, EPS);
    printf("PASS: test_curve_error_zero_degenerate\n");
    return 0;
}

static int test_negative_error_abs(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 0.0f, 10u, 40);       /* 到 straight */
    /* 负误差按幅值处理：−500 等效 |500|≥100 → 降速，与正误差同：1.0 − 0.04 = 0.96 */
    float v = SpeedPlan_Update(&sp, -500.0f, 10u);
    TEST_ASSERT_NEAR(v, 0.96f, EPS);
    printf("PASS: test_negative_error_abs\n");
    return 0;
}

static int test_reset_returns_to_min(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    ramp(&sp, 0.0f, 10u, 40);       /* 到 straight */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 1.0f, EPS);
    SpeedPlan_Reset(&sp);           /* 回 min，cfg 不变 */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 0.3f, EPS);
    /* cfg 未变：再 ramp 仍能回到 straight */
    ramp(&sp, 0.0f, 10u, 40);
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 1.0f, EPS);
    printf("PASS: test_reset_returns_to_min\n");
    return 0;
}

static int test_output_clamped_range(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();
    int i;

    SpeedPlan_Init(&sp, &cfg);
    /* 混合直道/入弯序列：每拍输出都必须落在 [min, straight] */
    for (i = 0; i < 200; i++) {
        float err = (i % 2 == 0) ? 0.0f : 300.0f;   /* 交替直道/大弯 */
        float v = SpeedPlan_Update(&sp, err, 10u);
        TEST_ASSERT_TRUE(v >= 0.3f - EPS);
        TEST_ASSERT_TRUE(v <= 1.0f + EPS);
    }
    printf("PASS: test_output_clamped_range\n");
    return 0;
}

static int test_getspeed_reflects_update_return(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    SpeedPlan_Init(&sp, &cfg);
    float v = SpeedPlan_Update(&sp, 0.0f, 30u);
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), v, EPS);
    printf("PASS: test_getspeed_reflects_update_return\n");
    return 0;
}

static int test_null_safe(void)
{
    SpeedPlan_T sp;
    SpeedPlan_Config_T cfg = base_cfg();

    /* NULL 各入口不崩 */
    SpeedPlan_Init(NULL, &cfg);
    SpeedPlan_Reset(NULL);
    TEST_ASSERT_NEAR(SpeedPlan_Update(NULL, 0.0f, 10u), 0.0f, EPS);
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(NULL), 0.0f, EPS);

    /* Init(&sp, NULL) 吸收：不改已有状态 */
    SpeedPlan_Init(&sp, &cfg);          /* current=0.3 */
    SpeedPlan_Init(&sp, NULL);          /* 吸收，无副作用 */
    TEST_ASSERT_NEAR(SpeedPlan_GetSpeed(&sp), 0.3f, EPS);
    printf("PASS: test_null_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_starts_at_min();
    failures += test_straight_ramps_up();
    failures += test_straight_reaches_straight();
    failures += test_curve_ramps_down();
    failures += test_curve_reaches_min();
    failures += test_interpolation_midpoint();
    failures += test_accel_slew_rate();
    failures += test_decel_slew_rate();
    failures += test_elapsed_zero_no_change();
    failures += test_large_elapsed_no_overshoot();
    failures += test_curve_error_zero_degenerate();
    failures += test_negative_error_abs();
    failures += test_reset_returns_to_min();
    failures += test_output_clamped_range();
    failures += test_getspeed_reflects_update_return();
    failures += test_null_safe();

    if (failures == 0) {
        printf("\nAll speed plan tests passed.\n");
        return 0;
    }

    printf("\n%d speed plan test(s) failed.\n", failures);
    return 1;
}
