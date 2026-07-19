/**
 * @file    test_motor_check.c
 * @brief   电机方向自检服务主机测试（W3-T3，契约 §23.3）。
 *
 * 链接组成：真实 motor_check.c + motor.c + fake_motor_hw.c + 测试。
 * 不链 fake_clock：motor_check 取 now_ms 参数注入。以 10ms 步进泵送（模拟真实 ~ms 主循环，
 * 避免单次大跳触发 motor.c 100ms 命令超时——那正是本服务每拍刷新所要防的看门狗）。
 *
 * 观测：FakeMotorHw_GetDir（±1/0 方向）、GetDutyPermille（幅值）、IsBrakeActive/GetBrakeCount。
 * slew=100‰/ms → ±200 约 2ms 到位；换向经零 + 5ms 死区 + 反向 ramp 归 motor.c，故翻相后多泵几拍再断言。
 */
#include "app/service/motor_check/motor_check.h"

#include "driver/motor/motor.h"

#include <stdint.h>
#include <stdio.h>

extern void FakeMotorHw_ResetLog(void);
extern int FakeMotorHw_GetWriteCount(void);
extern int FakeMotorHw_GetBrakeCount(void);
extern int16_t FakeMotorHw_GetDir(Motor_Id id);
extern uint16_t FakeMotorHw_GetDutyPermille(Motor_Id id);
extern bool FakeMotorHw_IsBrakeActive(Motor_Id id);

#define MOTOR_CHECK_OUTPUT 200u

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 从 Motor_Init 干净起点装配（ResetLog 抹去 Init 的写日志，便于观测 Start 后零命令）。 */
static void setup(void)
{
    Motor_Init();
    FakeMotorHw_ResetLog();
    MotorCheck_Start();
}

/* 以 10ms 步进泵送到 target_ms（含）。*now 出参跟踪当前时刻。 */
static void pump_until(uint32_t *now, uint32_t target_ms)
{
    while (*now < target_ms) {
        *now += 10u;
        MotorCheck_Update(*now);
    }
}

/* Start 后、首 Update 前：零电机命令（§8.1 init 安全态）。 */
static int test_start_no_command_before_update(void)
{
    setup();
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    printf("PASS: test_start_no_command_before_update\n");
    return 0;
}

/* FORWARD：两轮同向同幅 +200；长泵不掉速（证每拍刷新命令防 100ms 超时）。 */
static int test_forward_both_wheels_positive(void)
{
    uint32_t now = 1000u;

    setup();
    MotorCheck_Update(now);       /* 播种 */
    pump_until(&now, 1500u);      /* 泵 500ms（> 100ms 超时窗）仍稳定 */

    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_RIGHT) == 1);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == MOTOR_CHECK_OUTPUT);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == MOTOR_CHECK_OUTPUT);
    printf("PASS: test_forward_both_wheels_positive\n");
    return 0;
}

/* 2s 后翻 BACKWARD：两轮同向同幅 −200（反向经零+死区由 motor.c 承担，翻相后多泵再断言）。 */
static int test_flips_to_backward_after_2s(void)
{
    uint32_t now = 1000u;

    setup();
    MotorCheck_Update(now);       /* 播种，phase_base=1000 */
    pump_until(&now, 1500u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_LEFT) == 1); /* 仍 FORWARD */

    pump_until(&now, 3100u);      /* 跨 3000（=1000+2000）翻 BACKWARD + 泵过换向 */
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_LEFT) == -1);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_RIGHT) == -1);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == MOTOR_CHECK_OUTPUT);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == MOTOR_CHECK_OUTPUT);
    printf("PASS: test_flips_to_backward_after_2s\n");
    return 0;
}

/* 再 2s 循环回 FORWARD。 */
static int test_loops_back_to_forward(void)
{
    uint32_t now = 1000u;

    setup();
    MotorCheck_Update(now);       /* phase_base=1000 */
    pump_until(&now, 3100u);      /* → BACKWARD */
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_LEFT) == -1);

    pump_until(&now, 5200u);      /* 跨 5000（=3000+2000）翻回 FORWARD + 泵过换向 */
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_LEFT) == 1);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDir(MOTOR_RIGHT) == 1);
    printf("PASS: test_loops_back_to_forward\n");
    return 0;
}

/* Stop：两轮刹车（brake_active 且刹车 2 次、duty 0）。 */
static int test_stop_brakes_all(void)
{
    uint32_t now = 1000u;

    setup();
    MotorCheck_Update(now);
    pump_until(&now, 1300u);
    FakeMotorHw_ResetLog();

    MotorCheck_Stop();
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    TEST_ASSERT_TRUE(FakeMotorHw_GetBrakeCount() == MOTOR_COUNT);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_stop_brakes_all\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_start_no_command_before_update();
    failures += test_forward_both_wheels_positive();
    failures += test_flips_to_backward_after_2s();
    failures += test_loops_back_to_forward();
    failures += test_stop_brakes_all();

    if (failures != 0) {
        printf("motor_check service tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll motor_check service tests passed.\n");
    return 0;
}
