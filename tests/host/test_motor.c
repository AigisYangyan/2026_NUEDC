#include "driver/motor/motor.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern int FakeMotorHw_GetWriteCount(void);
extern int FakeMotorHw_GetWriteDirCount(void);
extern int FakeMotorHw_GetWriteDutyCount(void);
extern int FakeMotorHw_GetStartPwmCount(void);
extern int FakeMotorHw_GetBrakeCount(void);
extern int16_t FakeMotorHw_GetDir(Motor_Id id);
extern uint16_t FakeMotorHw_GetDutyPermille(Motor_Id id);
extern bool FakeMotorHw_IsBrakeActive(Motor_Id id);
extern int FakeMotorHw_GetBrakeSeq(Motor_Id id);
extern int FakeMotorHw_GetDutySeq(Motor_Id id);
extern void FakeMotorHw_ResetLog(void);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define EXPECTED_SLEW_PER_MS 100
#define EXPECTED_REVERSE_DEADTIME_MS 5u
#define EXPECTED_TIMEOUT_MS 100u

static void motor_test_reset(void)
{
    FakeMotorHw_ResetLog();
    Motor_Init();
}

static int test_init_starts_safe(void)
{
    motor_test_reset();

    TEST_ASSERT(FakeMotorHw_GetStartPwmCount() == 1);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 0);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_RIGHT) == 0);
    TEST_ASSERT(FakeMotorHw_IsBrakeActive(MOTOR_LEFT) == false);
    TEST_ASSERT(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT) == false);

    printf("PASS: test_init_starts_safe\n");
    return 0;
}

static int test_out_of_range_rejected_and_state_unchanged(void)
{
    motor_test_reset();

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 100) == true);
    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 100u);
    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 1001) == false);
    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 100u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);

    printf("PASS: test_out_of_range_rejected_and_state_unchanged\n");
    return 0;
}

static int test_slew_scales_with_elapsed(void)
{
    motor_test_reset();

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 1000) == true);
    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == EXPECTED_SLEW_PER_MS);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);

    Motor_Update(2u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == (EXPECTED_SLEW_PER_MS * 3));
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) < 1000u);

    printf("PASS: test_slew_scales_with_elapsed\n");
    return 0;
}

static int test_reverse_requires_zero_and_deadtime(void)
{
    uint32_t i;

    motor_test_reset();

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 800) == true);
    Motor_Update(8u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 800u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, -800) == true);

    for (i = 0; i < 7u; ++i) {
        Motor_Update(1u);
        TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);
        TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == (uint16_t)(700u - (i * 100u)));
    }

    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 0);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);

    for (i = 0; i < EXPECTED_REVERSE_DEADTIME_MS; ++i) {
        Motor_Update(1u);
        TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 0);
        TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    }

    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == -1);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == EXPECTED_SLEW_PER_MS);

    printf("PASS: test_reverse_requires_zero_and_deadtime\n");
    return 0;
}

static int test_same_direction_command_does_not_shorten_deadtime(void)
{
    uint32_t i;

    motor_test_reset();

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 800) == true);
    Motor_Update(8u);
    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, -800) == true);

    for (i = 0; i < 8u; ++i) {
        Motor_Update(1u);
    }

    for (i = 0; i < 2u; ++i) {
        Motor_Update(1u);
        TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 0);
        TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    }

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, -400) == true);

    for (i = 0; i < (EXPECTED_REVERSE_DEADTIME_MS - 2u); ++i) {
        Motor_Update(1u);
        TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 0);
        TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    }

    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == -1);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == EXPECTED_SLEW_PER_MS);

    printf("PASS: test_same_direction_command_does_not_shorten_deadtime\n");
    return 0;
}

static int test_timeout_stops_output(void)
{
    motor_test_reset();

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 500) == true);
    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 100u);

    Motor_Update(EXPECTED_TIMEOUT_MS - 2u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);

    Motor_Update(1u);
    TEST_ASSERT(FakeMotorHw_GetDir(MOTOR_LEFT) == 0);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT(FakeMotorHw_IsBrakeActive(MOTOR_LEFT) == false);

    printf("PASS: test_timeout_stops_output\n");
    return 0;
}

static int test_brake_is_immediate_and_bypasses_slew(void)
{
    motor_test_reset();

    TEST_ASSERT(Motor_SetOutput(MOTOR_LEFT, 500) == true);
    TEST_ASSERT(Motor_SetOutput(MOTOR_RIGHT, -500) == true);
    Motor_Update(2u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 200u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 200u);

    Motor_BrakeAll();

    TEST_ASSERT(FakeMotorHw_GetBrakeCount() == 2);
    TEST_ASSERT(FakeMotorHw_IsBrakeActive(MOTOR_LEFT) == true);
    TEST_ASSERT(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT) == true);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    TEST_ASSERT(FakeMotorHw_GetBrakeSeq(MOTOR_LEFT) < FakeMotorHw_GetDutySeq(MOTOR_LEFT));
    TEST_ASSERT(FakeMotorHw_GetBrakeSeq(MOTOR_RIGHT) < FakeMotorHw_GetDutySeq(MOTOR_RIGHT));

    printf("PASS: test_brake_is_immediate_and_bypasses_slew\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_starts_safe();
    failures += test_out_of_range_rejected_and_state_unchanged();
    failures += test_slew_scales_with_elapsed();
    failures += test_reverse_requires_zero_and_deadtime();
    failures += test_same_direction_command_does_not_shorten_deadtime();
    failures += test_timeout_stops_output();
    failures += test_brake_is_immediate_and_bypasses_slew();

    if (failures == 0) {
        printf("INFO: safety placeholders reverse_deadtime_ms=%u timeout_ms=%u slew_per_ms=%d (pending T3 hardware measurement)\n",
               EXPECTED_REVERSE_DEADTIME_MS,
               EXPECTED_TIMEOUT_MS,
               EXPECTED_SLEW_PER_MS);
        printf("\nAll motor tests passed.\n");
        return 0;
    }

    printf("\n%d motor test(s) failed.\n", failures);
    return 1;
}
