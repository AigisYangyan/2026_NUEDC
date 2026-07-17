/**
 * @file    test_chassis.c
 * @brief   Host tests for the chassis speed-loop service (S01).
 *
 * 契约回顾（phase4 计划表 §6）：
 * - Chassis_Update() 自取 Clock_NowMs，不足 10ms 直接返回（零硬件写入）；
 *   到期执行 Encoder_Update(elapsed) → 快照 → 双轮增量 PID → Motor_SetOutput → Motor_Update(elapsed)
 * - Chassis_Init 不发任何电机命令；Chassis_Stop = 目标清零 + PID 复位 + Motor_BrakeAll
 * - 输出限幅唯一所有者 = Pid cfg（±1000）；slew/换向/超时归 motor.c，本服务不复做
 */
#include "app/service/chassis/chassis.h"

#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/motor/motor.h"

#include <math.h>
#include <stdio.h>

/* fake 观测/注入接口（fake_motor_hw.c / fake_board_gpio.c / fake_clock.c） */
extern void FakeMotorHw_ResetLog(void);
extern int FakeMotorHw_GetWriteCount(void);
extern int FakeMotorHw_GetBrakeCount(void);
extern uint16_t FakeMotorHw_GetDutyPermille(Motor_Id id);
extern bool FakeMotorHw_IsBrakeActive(Motor_Id id);
extern void FakeBoardGpio_SetRaw(int32_t left, int32_t right);
extern void FakeBoardGpio_SetSnapshotFail(bool fail);
extern void FakeClock_Set(uint32_t now_ms);
extern void FakeClock_Advance(uint32_t delta_ms);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_FLOAT_NEAR(actual, expected, epsilon) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (double)(actual), (double)(expected), (double)(epsilon), \
               __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 标准装配序：与 SysInit 的职责一致（Clock/Motor/Encoder 先于 Service） */
static void setup(void)
{
    Clock_Init();
    FakeClock_Set(1000u);
    FakeBoardGpio_SetRaw(0, 0);
    Motor_Init();
    Encoder_Init();
    FakeMotorHw_ResetLog();
    Chassis_Init();
}

/* 安全项：Init 后不得有任何电机命令（上电安全态归 Motor_Init，服务初始化必须静默） */
static int test_init_is_silent(void)
{
    Chassis_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(FakeMotorHw_GetBrakeCount() == 0);
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    printf("PASS: test_init_is_silent\n");
    return 0;
}

/* 安全项：未到控制周期的 Update 必须零硬件写入 */
static int test_update_before_period_is_noop(void)
{
    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 1.0f);
    FakeClock_Advance(9u);
    Chassis_Update();
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    printf("PASS: test_update_before_period_is_noop\n");
    return 0;
}

/* 到期路径：目标-反馈误差经 PID 变成电机输出（增量式首拍 out = kp*e） */
static int test_update_at_period_drives_motor(void)
{
    Chassis_Telemetry_T t;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 1.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 100.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_RIGHT], 100.0f, 1e-3f);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) > 0u);
    printf("PASS: test_update_at_period_drives_motor\n");
    return 0;
}

/* 增益生效：kp 加倍 → 服务级输出加倍 */
static int test_gain_scales_output(void)
{
    Chassis_Telemetry_T t1;
    Chassis_Telemetry_T t2;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 0.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    Chassis_GetTelemetry(&t1);

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 200.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 0.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    Chassis_GetTelemetry(&t2);

    TEST_ASSERT_FLOAT_NEAR(t2.pid_out[CHASSIS_SIDE_LEFT],
                           2.0f * t1.pid_out[CHASSIS_SIDE_LEFT], 1e-3f);
    printf("PASS: test_gain_scales_output\n");
    return 0;
}

/* 左右独立：右轮增益为零时右轮不出力 */
static int test_per_side_independent(void)
{
    Chassis_Telemetry_T t;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    /* 右轮保持 Init 的零增益 */
    Chassis_SetTargetMps(1.0f, 1.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 100.0f, 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_per_side_independent\n");
    return 0;
}

/* elapsed 正确传递：同样 100 脉冲，20ms 窗口的速度是 10ms 窗口的一半。
 * 用右轮断言正速度（Driver 唯一方向修正点 s_direction_sign = {-1,+1}，
 * 左轮原始计数正向对应车体后退——测试尊重 Driver 出口口径）。 */
static int test_feedback_scales_with_elapsed(void)
{
    Chassis_Telemetry_T t10;
    Chassis_Telemetry_T t20;

    setup();
    FakeClock_Advance(10u);
    FakeBoardGpio_SetRaw(100, 100);
    Chassis_Update();
    Chassis_GetTelemetry(&t10);
    TEST_ASSERT_TRUE(t10.feedback_mps[CHASSIS_SIDE_RIGHT] > 0.0f);
    TEST_ASSERT_TRUE(t10.feedback_mps[CHASSIS_SIDE_LEFT] < 0.0f);

    setup();
    FakeClock_Advance(20u);
    FakeBoardGpio_SetRaw(100, 100);
    Chassis_Update();
    Chassis_GetTelemetry(&t20);

    TEST_ASSERT_FLOAT_NEAR(t20.feedback_mps[CHASSIS_SIDE_RIGHT],
                           0.5f * t10.feedback_mps[CHASSIS_SIDE_RIGHT],
                           1e-4f * t10.feedback_mps[CHASSIS_SIDE_RIGHT] + 1e-6f);
    printf("PASS: test_feedback_scales_with_elapsed\n");
    return 0;
}

/* 周期门控基准不被早到的 Update 重置：6ms(不跑)+6ms(累计12ms 跑) */
static int test_partial_periods_accumulate(void)
{
    Chassis_Telemetry_T t;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 0.0f);
    FakeClock_Advance(6u);
    Chassis_Update();
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    FakeClock_Advance(6u);
    Chassis_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 100.0f, 1e-3f);
    printf("PASS: test_partial_periods_accumulate\n");
    return 0;
}

/* 安全项：Stop = 刹车 + 目标/PID 清零（确定性停止接口，§8.1） */
static int test_stop_brakes_and_clears(void)
{
    Chassis_Telemetry_T t;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 1.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    Chassis_Stop();
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    printf("PASS: test_stop_brakes_and_clears\n");
    return 0;
}

/* 重新 Init 清运行史：增益回到 0，输出归零 */
static int test_reinit_clears_history(void)
{
    Chassis_Telemetry_T t;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 0.0f);
    FakeClock_Advance(10u);
    Chassis_Update();

    Chassis_Init();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    Chassis_SetTargetMps(1.0f, 0.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    printf("PASS: test_reinit_clears_history\n");
    return 0;
}

/* 时基回绕：Clock_NowMs 跨 2^32 后无符号减法门控仍工作 */
static int test_clock_wrap_still_updates(void)
{
    Chassis_Telemetry_T t;

    Clock_Init();
    FakeClock_Set(0xFFFFFFFAu);
    FakeBoardGpio_SetRaw(0, 0);
    Motor_Init();
    Encoder_Init();
    FakeMotorHw_ResetLog();
    Chassis_Init();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 0.0f);
    FakeClock_Advance(12u); /* 0xFFFFFFFA + 12 回绕到 6 */
    Chassis_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 100.0f, 1e-3f);
    printf("PASS: test_clock_wrap_still_updates\n");
    return 0;
}

/* 遥测一致性：目标设置立即可见，不依赖 Update */
static int test_target_reflected_in_telemetry(void)
{
    Chassis_Telemetry_T t;

    setup();
    Chassis_SetTargetMps(0.5f, -0.5f);
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.5f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], -0.5f, 1e-6f);
    printf("PASS: test_target_reflected_in_telemetry\n");
    return 0;
}

/* 安全项：持续采样失败 → 不刷新电机命令，Driver 命令超时（100ms）把输出归零 */
static int test_sampling_failure_timeout_stops(void)
{
    int i;

    setup();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 1.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) > 0u);

    FakeBoardGpio_SetSnapshotFail(true);
    /* 300ms 持续失败：远超 Driver 100ms 命令超时 + slew 收敛时间 */
    for (i = 0; i < 30; i++) {
        FakeClock_Advance(10u);
        Chassis_Update();
    }
    FakeBoardGpio_SetSnapshotFail(false);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_sampling_failure_timeout_stops\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_is_silent();
    failures += test_update_before_period_is_noop();
    failures += test_update_at_period_drives_motor();
    failures += test_gain_scales_output();
    failures += test_per_side_independent();
    failures += test_feedback_scales_with_elapsed();
    failures += test_partial_periods_accumulate();
    failures += test_stop_brakes_and_clears();
    failures += test_reinit_clears_history();
    failures += test_clock_wrap_still_updates();
    failures += test_target_reflected_in_telemetry();
    failures += test_sampling_failure_timeout_stops();

    if (failures != 0) {
        printf("%d chassis test(s) failed.\n", failures);
        return 1;
    }
    printf("All chassis tests passed.\n");
    return 0;
}
