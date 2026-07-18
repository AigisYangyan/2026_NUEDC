/**
 * @file    test_heading.c
 * @brief   Host tests for IMU yaw unwrap (M01, heading.c).
 *
 * 约定回顾（契约 §14）：
 * - 首样本 seed 并原值返回；此后连续角 = yaw + wrap_count×360。
 * - 跨界：delta<-180 → wrap++（越 -180 向正）；delta>180 → wrap--（越 +180 向负）。
 * - unwrap 无损、不滤波；ctx==NULL 返回传入值。
 */
#include "middleware/odometry/heading.h"

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

/* 首样本 seed：任意起始 yaw 原值返回，wrap 从 0 起。 */
static int test_first_sample_seeds_and_returns_raw(void)
{
    Heading_T h;
    Heading_Reset(&h);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 37.0f), 37.0f, 1e-4f);
    /* 同值再送：delta=0，无跨界，仍返回原值 */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 37.0f), 37.0f, 1e-4f);
    printf("PASS: test_first_sample_seeds_and_returns_raw\n");
    return 0;
}

/* CCW 连续旋转：+179 → -179 跨界，连续角单调越过 +180。 */
static int test_ccw_crossing_monotonic_past_plus180(void)
{
    Heading_T h;
    Heading_Reset(&h);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 179.0f), 179.0f, 1e-4f);
    /* -179 相对 179：delta=-358 < -180 → wrap++ → 连续角 = -179 + 360 = 181 */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -179.0f), 181.0f, 1e-4f);
    /* 继续到 -170：连续角 = -170 + 360 = 190（仍单调增） */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -170.0f), 190.0f, 1e-4f);
    printf("PASS: test_ccw_crossing_monotonic_past_plus180\n");
    return 0;
}

/* CW 连续旋转：-179 → +179 跨界，连续角单调越过 -180。 */
static int test_cw_crossing_monotonic_past_minus180(void)
{
    Heading_T h;
    Heading_Reset(&h);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -179.0f), -179.0f, 1e-4f);
    /* +179 相对 -179：delta=358 > 180 → wrap-- → 连续角 = 179 - 360 = -181 */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 179.0f), -181.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 170.0f), -190.0f, 1e-4f);
    printf("PASS: test_cw_crossing_monotonic_past_minus180\n");
    return 0;
}

/* 多圈同向累计：连续三次 CCW 跨界 → 连续角逼近 +3 圈。 */
static int test_multi_turn_accumulation(void)
{
    Heading_T h;
    Heading_Reset(&h);
    /* 从 0 起，每步 +120，走满 3 圈回到 0：0,120,240(=-120),360(=0),... */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 0.0f),   0.0f,   1e-4f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 120.0f), 120.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -120.0f), 240.0f, 1e-4f); /* delta=-240 → wrap++ */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 0.0f),   360.0f, 1e-4f);  /* delta=120，无跨界 */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 120.0f), 480.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -120.0f), 600.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 0.0f),   720.0f, 1e-4f);  /* 满 2 圈 */
    printf("PASS: test_multi_turn_accumulation\n");
    return 0;
}

/* 小幅抖动不触发跨界：wrap_count 保持，连续角=原值。 */
static int test_small_deltas_no_wrap(void)
{
    Heading_T h;
    Heading_Reset(&h);
    (void)Heading_Unwrap(&h, 10.0f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -10.0f), -10.0f, 1e-4f); /* delta=-20，无跨界 */
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, 90.0f),  90.0f,  1e-4f);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -90.0f), -90.0f, 1e-4f); /* delta=-180，非严格 <-180，无跨界 */
    printf("PASS: test_small_deltas_no_wrap\n");
    return 0;
}

/* Reset 清零：跨界后 Reset，再送新样本重新 seed（无残留圈数）。 */
static int test_reset_reseeds(void)
{
    Heading_T h;
    Heading_Reset(&h);
    (void)Heading_Unwrap(&h, 179.0f);
    (void)Heading_Unwrap(&h, -179.0f); /* wrap_count 现为 +1 */
    Heading_Reset(&h);
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(&h, -179.0f), -179.0f, 1e-4f); /* 重新 seed，无 +360 残留 */
    printf("PASS: test_reset_reseeds\n");
    return 0;
}

/* NULL 上下文：返回传入值，无崩溃。 */
static int test_null_ctx_returns_input(void)
{
    TEST_ASSERT_FLOAT_NEAR(Heading_Unwrap(NULL, 42.0f), 42.0f, 1e-4f);
    Heading_Reset(NULL); /* 不崩 */
    printf("PASS: test_null_ctx_returns_input\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_first_sample_seeds_and_returns_raw();
    failures += test_ccw_crossing_monotonic_past_plus180();
    failures += test_cw_crossing_monotonic_past_minus180();
    failures += test_multi_turn_accumulation();
    failures += test_small_deltas_no_wrap();
    failures += test_reset_reseeds();
    failures += test_null_ctx_returns_input();

    if (failures == 0) {
        printf("\nAll heading tests passed.\n");
        return 0;
    }

    printf("\n%d heading test(s) failed.\n", failures);
    return 1;
}
