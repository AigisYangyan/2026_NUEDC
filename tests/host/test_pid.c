/**
 * @file    test_pid.c
 * @brief   Host tests for the by-value dual-motor PID interface.
 */
#include "middleware/pid/pid.h"

#include <math.h>
#include <stdio.h>

#define TEST_ASSERT_FLOAT_NEAR(actual, expected, epsilon) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (actual), (expected), (epsilon), __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static void configure_gains(float left_kp, float left_ki, float left_kd,
                            float right_kp, float right_ki, float right_kd)
{
    pid_Init();
    g_tLeftMotorPID.kp = left_kp;
    g_tLeftMotorPID.ki = left_ki;
    g_tLeftMotorPID.kd = left_kd;
    g_tRightMotorPID.kp = right_kp;
    g_tRightMotorPID.ki = right_ki;
    g_tRightMotorPID.kd = right_kd;
}

static int test_signed_error_direction(void)
{
    float left_out;
    float right_out;

    configure_gains(100.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f);
    pid_closeloop_motor(1.0f, -1.0f, 0.0f, 0.0f,
                        &left_out, &right_out);

    TEST_ASSERT_FLOAT_NEAR(left_out, 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, -100.0f, 1e-5f);
    printf("PASS: test_signed_error_direction\n");
    return 0;
}

static int test_output_saturation(void)
{
    float left_out;
    float right_out;

    configure_gains(2000.0f, 0.0f, 0.0f, 2000.0f, 0.0f, 0.0f);
    pid_closeloop_motor(1.0f, -1.0f, 0.0f, 0.0f,
                        &left_out, &right_out);

    TEST_ASSERT_FLOAT_NEAR(left_out, 1000.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, -1000.0f, 1e-5f);
    printf("PASS: test_output_saturation\n");
    return 0;
}

static int test_zero_error_steady_state(void)
{
    float left_out;
    float right_out;

    configure_gains(100.0f, 10.0f, 5.0f, 100.0f, 10.0f, 5.0f);
    pid_closeloop_motor(0.5f, -0.5f, 0.5f, -0.5f,
                        &left_out, &right_out);
    TEST_ASSERT_FLOAT_NEAR(left_out, 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, 0.0f, 1e-5f);

    pid_closeloop_motor(0.5f, -0.5f, 0.5f, -0.5f,
                        &left_out, &right_out);
    TEST_ASSERT_FLOAT_NEAR(left_out, 0.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, 0.0f, 1e-5f);
    printf("PASS: test_zero_error_steady_state\n");
    return 0;
}

static int test_incremental_progression(void)
{
    float left_out;
    float right_out;

    configure_gains(0.0f, 10.0f, 0.0f, 0.0f, 10.0f, 0.0f);
    pid_closeloop_motor(1.0f, -1.0f, 0.0f, 0.0f,
                        &left_out, &right_out);
    TEST_ASSERT_FLOAT_NEAR(left_out, 10.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, -10.0f, 1e-5f);

    pid_closeloop_motor(1.0f, -1.0f, 0.0f, 0.0f,
                        &left_out, &right_out);
    TEST_ASSERT_FLOAT_NEAR(left_out, 20.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, -20.0f, 1e-5f);
    printf("PASS: test_incremental_progression\n");
    return 0;
}

static int test_left_right_isolation(void)
{
    float left_out;
    float right_out;

    configure_gains(100.0f, 0.0f, 0.0f, 200.0f, 0.0f, 0.0f);
    pid_closeloop_motor(1.0f, 0.0f, 0.0f, 0.0f,
                        &left_out, &right_out);
    TEST_ASSERT_FLOAT_NEAR(left_out, 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, 0.0f, 1e-5f);

    pid_closeloop_motor(1.0f, -1.0f, 0.0f, 0.0f,
                        &left_out, &right_out);
    TEST_ASSERT_FLOAT_NEAR(left_out, 100.0f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(right_out, -200.0f, 1e-5f);
    printf("PASS: test_left_right_isolation\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_signed_error_direction();
    failures += test_output_saturation();
    failures += test_zero_error_steady_state();
    failures += test_incremental_progression();
    failures += test_left_right_isolation();

    if (failures == 0) {
        printf("\nAll PID tests passed.\n");
        return 0;
    }

    printf("\n%d PID test(s) failed.\n", failures);
    return 1;
}
