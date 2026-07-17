/**
 * @file    test_pid.c
 * @brief   Host tests for the caller-owned-context PID middleware (M01).
 *
 * 覆盖：
 * - 与旧实现行为等值的 5 个用例（符号方向/饱和/稳态零误差/增量累积/双实例隔离）
 * - 位置式公式、显式/推导积分限幅、Reset 语义、SetGains/SetLimits、
 *   NaN 回退、微分一阶低通、遥测读出
 */
#include "middleware/pid/pid.h"

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

static const Pid_Config_T k_motor_like_cfg = {
    .kp = 0.0f, .ki = 0.0f, .kd = 0.0f,
    .out_limit = 1000.0f,
    .integral_limit = 0.0f,   /* 推导为 out_limit*3.5 */
    .d_filter_alpha = 1.0f,
};

static void init_pair(Pid_T *left, Pid_T *right,
                      float lkp, float lki, float lkd,
                      float rkp, float rki, float rkd)
{
    Pid_Init(left, &k_motor_like_cfg);
    Pid_Init(right, &k_motor_like_cfg);
    Pid_SetGains(left, lkp, lki, lkd);
    Pid_SetGains(right, rkp, rki, rkd);
}

/* ---- 与旧实现行为等值的用例 --------------------------------------------- */

static int test_signed_error_direction(void)
{
    Pid_T left, right;

    init_pair(&left, &right, 100.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&left, 1.0f, 0.0f), 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&right, -1.0f, 0.0f), -100.0f, 1e-5f);
    printf("PASS: test_signed_error_direction\n");
    return 0;
}

static int test_output_saturation(void)
{
    Pid_T left, right;

    init_pair(&left, &right, 2000.0f, 0.0f, 0.0f, 2000.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&left, 1.0f, 0.0f), 1000.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&right, -1.0f, 0.0f), -1000.0f, 1e-5f);
    printf("PASS: test_output_saturation\n");
    return 0;
}

static int test_zero_error_steady_state(void)
{
    Pid_T pid;

    Pid_Init(&pid, &k_motor_like_cfg);
    Pid_SetGains(&pid, 100.0f, 10.0f, 5.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 0.5f, 0.5f), 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 0.5f, 0.5f), 0.0f, 1e-5f);
    printf("PASS: test_zero_error_steady_state\n");
    return 0;
}

static int test_incremental_progression(void)
{
    Pid_T pid;

    Pid_Init(&pid, &k_motor_like_cfg);
    Pid_SetGains(&pid, 0.0f, 10.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 10.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 20.0f, 1e-5f);
    printf("PASS: test_incremental_progression\n");
    return 0;
}

static int test_instance_isolation(void)
{
    Pid_T left, right;

    init_pair(&left, &right, 100.0f, 0.0f, 0.0f, 200.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&left, 1.0f, 0.0f), 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&right, 0.0f, 0.0f), 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&left, 1.0f, 0.0f), 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&right, -1.0f, 0.0f), -200.0f, 1e-5f);
    printf("PASS: test_instance_isolation\n");
    return 0;
}

/* ---- 位置式与配置语义 ---------------------------------------------------- */

static int test_positional_terms_and_telemetry(void)
{
    Pid_T pid;
    Pid_Telemetry_T tele;
    Pid_Config_T cfg = k_motor_like_cfg;

    cfg.kp = 2.0f;
    cfg.ki = 0.5f;
    Pid_Init(&pid, &cfg);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 4.0f, 0.0f), 10.0f, 1e-5f);

    Pid_GetTelemetry(&pid, &tele);
    TEST_ASSERT_FLOAT_NEAR(tele.p_out, 8.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(tele.i_out, 2.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(tele.d_out, 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(tele.out, 10.0f, 1e-5f);
    printf("PASS: test_positional_terms_and_telemetry\n");
    return 0;
}

static int test_positional_explicit_integral_limit(void)
{
    Pid_T pid;
    Pid_Config_T cfg = k_motor_like_cfg;

    cfg.ki = 1.0f;
    cfg.out_limit = 100.0f;
    cfg.integral_limit = 3.0f;
    Pid_Init(&pid, &cfg);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 2.0f, 0.0f), 2.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 2.0f, 0.0f), 3.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 2.0f, 0.0f), 3.0f, 1e-5f);
    printf("PASS: test_positional_explicit_integral_limit\n");
    return 0;
}

static int test_positional_derived_integral_limit(void)
{
    Pid_T pid;
    Pid_Config_T cfg = k_motor_like_cfg;

    /* integral_limit<=0 时按 out_limit*3.5=35 推导（沿用旧实现语义） */
    cfg.ki = 0.1f;
    cfg.out_limit = 10.0f;
    cfg.integral_limit = 0.0f;
    Pid_Init(&pid, &cfg);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 10.0f, 0.0f), 1.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 10.0f, 0.0f), 2.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 10.0f, 0.0f), 3.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 10.0f, 0.0f), 3.5f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 10.0f, 0.0f), 3.5f, 1e-5f);
    printf("PASS: test_positional_derived_integral_limit\n");
    return 0;
}

static int test_reset_clears_history_keeps_config(void)
{
    Pid_T pid;

    Pid_Init(&pid, &k_motor_like_cfg);
    Pid_SetGains(&pid, 0.0f, 10.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 10.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 20.0f, 1e-5f);

    Pid_Reset(&pid);
    /* 运行史清零：同序列输出与新建实例一致；增益/限幅配置保留 */
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 10.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 20.0f, 1e-5f);
    printf("PASS: test_reset_clears_history_keeps_config\n");
    return 0;
}

static int test_set_gains_takes_effect(void)
{
    Pid_T pid;

    Pid_Init(&pid, &k_motor_like_cfg);
    Pid_SetGains(&pid, 100.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 100.0f, 1e-5f);

    Pid_Reset(&pid);
    Pid_SetGains(&pid, 300.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 300.0f, 1e-5f);
    printf("PASS: test_set_gains_takes_effect\n");
    return 0;
}

static int test_set_limits_takes_effect(void)
{
    Pid_T pid;
    Pid_Config_T cfg = k_motor_like_cfg;

    cfg.kp = 100.0f;
    cfg.out_limit = 100.0f;
    Pid_Init(&pid, &cfg);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 2.0f, 0.0f), 100.0f, 1e-5f);

    Pid_SetLimits(&pid, 50.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdatePositional(&pid, 2.0f, 0.0f), 50.0f, 1e-5f);
    printf("PASS: test_set_limits_takes_effect\n");
    return 0;
}

static int test_nan_feedback_falls_back_to_last_output(void)
{
    Pid_T pid;

    Pid_Init(&pid, &k_motor_like_cfg);
    Pid_SetGains(&pid, 100.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, NAN), 100.0f, 1e-5f);
    printf("PASS: test_nan_feedback_falls_back_to_last_output\n");
    return 0;
}

static int test_derivative_low_pass_filter(void)
{
    Pid_T pid;
    Pid_Config_T cfg = k_motor_like_cfg;

    cfg.kd = 1.0f;
    cfg.d_filter_alpha = 0.5f;
    Pid_Init(&pid, &cfg);
    /* raw_d=1 → d=0.5；raw_d=-1 → d=0.5*(-1)+0.5*0.5=-0.25，out=0.5-0.25 */
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 0.5f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(Pid_UpdateIncremental(&pid, 1.0f, 0.0f), 0.25f, 1e-5f);
    printf("PASS: test_derivative_low_pass_filter\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_signed_error_direction();
    failures += test_output_saturation();
    failures += test_zero_error_steady_state();
    failures += test_incremental_progression();
    failures += test_instance_isolation();
    failures += test_positional_terms_and_telemetry();
    failures += test_positional_explicit_integral_limit();
    failures += test_positional_derived_integral_limit();
    failures += test_reset_clears_history_keeps_config();
    failures += test_set_gains_takes_effect();
    failures += test_set_limits_takes_effect();
    failures += test_nan_feedback_falls_back_to_last_output();
    failures += test_derivative_low_pass_filter();

    if (failures == 0) {
        printf("\nAll PID tests passed.\n");
        return 0;
    }

    printf("\n%d PID test(s) failed.\n", failures);
    return 1;
}
