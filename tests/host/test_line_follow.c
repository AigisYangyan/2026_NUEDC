/**
 * @file    test_line_follow.c
 * @brief   Host tests for the line-follow outer-loop service (S02).
 *
 * 组合：real line_follow + lost_line + chassis + gray + track_error + pid
 *       + encoder + motor，只 fake 端口层（gray_port / board_gpio / motor_hw / clock）。
 * 关键口径：+误差 = 线在车右 → 左快右慢（left = base + c, right = base − c）；
 * 差速修正 c 经外环 Pid out_limit（= cfg.diff_limit_mps）限幅。
 * 底盘内环增益在本测试中保持 0（S01 语义：目标可见于 Chassis 遥测，占空为 0），
 * 断言以 Chassis_GetTelemetry 的 target_mps 为主。
 */
#include "app/service/line_follow/line_follow.h"

#include "app/service/chassis/chassis.h"
#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/motor/motor.h"

#include <math.h>
#include <stdio.h>

extern void FakeMotorHw_ResetLog(void);
extern uint16_t FakeMotorHw_GetDutyPermille(Motor_Id id);
extern bool FakeMotorHw_IsBrakeActive(Motor_Id id);
extern void FakeBoardGpio_SetRaw(int32_t left, int32_t right);
extern void FakeClock_Set(uint32_t now_ms);
extern void FakeClock_Advance(uint32_t delta_ms);
extern void FakeGrayPort_Reset(void);
extern void FakeGrayPort_SetDarkChannels(uint16_t channel_bitmap);
extern int FakeGrayPort_GetReadCount(void);

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

/* 标准配置：pitch 10mm、bit0=左、基速 0.5、差速限幅 0.6、回退 27mm、超时 100ms */
static const LineFollow_Config_T k_cfg = {
    .pitch_mm = 10.0f,
    .bit0_is_left = true,
    .base_speed_mps = 0.5f,
    .diff_limit_mps = 0.6f,
    .recovery_error_mm = 27.0f,
    .lost_timeout_ms = 100u,
};

static void setup(const LineFollow_Config_T *cfg)
{
    Clock_Init();
    FakeClock_Set(1000u);
    FakeGrayPort_Reset();
    FakeBoardGpio_SetRaw(0, 0);
    Motor_Init();
    Encoder_Init();
    FakeMotorHw_ResetLog();
    Chassis_Init();
    LineFollow_Init(cfg);
}

/* 推进一个 10ms 控制拍 */
static void tick(void)
{
    FakeClock_Advance(10u);
    LineFollow_Update();
}

static int test_start_gate_on_invalid_config(void)
{
    LineFollow_Config_T bad = k_cfg;

    bad.pitch_mm = 0.0f;
    setup(&bad);
    TEST_ASSERT_TRUE(!LineFollow_Start());
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_IDLE);

    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_TRACKING);
    printf("PASS: test_start_gate_on_invalid_config\n");
    return 0;
}

/* 安全项：未 Start 的 Update 不设底盘目标、不采样灰度 */
static int test_idle_update_is_passive(void)
{
    Chassis_Telemetry_T ct;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    FakeGrayPort_SetDarkChannels(0x0060u);
    tick();
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 0);
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_IDLE);
    printf("PASS: test_idle_update_is_passive\n");
    return 0;
}

/* 正对线中央（bit5+bit6 对称）→ 误差 0 → 双轮 = 基速 */
static int test_center_line_goes_straight(void)
{
    Chassis_Telemetry_T ct;
    LineFollow_Telemetry_T lt;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0060u); /* bit5+bit6 */
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.error_mm, 0.0f, 1e-4f);
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.5f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.5f, 1e-5f);
    printf("PASS: test_center_line_goes_straight\n");
    return 0;
}

/* 差速符号：bit0=左时 channel9 = 右侧，误差 +35mm → 左快右慢 */
static int test_diff_sign_line_right(void)
{
    Chassis_Telemetry_T ct;
    LineFollow_Telemetry_T lt;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9)); /* index9: (9-5.5)*10=+35 */
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.error_mm, 35.0f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(lt.diff_cmd_mps, 0.35f, 1e-5f);
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.85f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.15f, 1e-5f);
    printf("PASS: test_diff_sign_line_right\n");
    return 0;
}

/* bit0_is_left=false：同一 channel9 变成左侧（index2，误差 −35）→ 右快左慢 */
static int test_bit0_reversal_flips_side(void)
{
    Chassis_Telemetry_T ct;
    LineFollow_Config_T cfg = k_cfg;

    cfg.bit0_is_left = false;
    setup(&cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9));
    tick();
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.15f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.85f, 1e-5f);
    printf("PASS: test_bit0_reversal_flips_side\n");
    return 0;
}

/* 差速限幅：kp 放大 10 倍 → c 原始 3.5 → 被 out_limit 0.6 截断 */
static int test_diff_limit_clamps(void)
{
    Chassis_Telemetry_T ct;

    setup(&k_cfg);
    LineFollow_SetGains(0.1f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9));
    tick();
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 1.1f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], -0.1f, 1e-5f);
    printf("PASS: test_diff_limit_clamps\n");
    return 0;
}

/* 丢线恢复：先见右侧线，丢线后回退误差 = +27mm，方向不变 */
static int test_lost_recovery_keeps_direction(void)
{
    LineFollow_Telemetry_T lt;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9));
    tick();
    FakeGrayPort_SetDarkChannels(0x0000u); /* 丢线 */
    tick();
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_RECOVERING);
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.error_mm, 27.0f, 1e-4f);
    printf("PASS: test_lost_recovery_keeps_direction\n");
    return 0;
}

/* 安全项：丢线超时 → LOST，底盘目标清零、输出归零，且不再采样 */
static int test_lost_timeout_stops_chassis(void)
{
    Chassis_Telemetry_T ct;
    int i;
    int reads_at_lost;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9));
    tick();
    FakeGrayPort_SetDarkChannels(0x0000u);
    for (i = 0; i < 10; i++) { /* 100ms 丢线：达到 lost_timeout_ms */
        tick();
    }
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_LOST);
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    reads_at_lost = FakeGrayPort_GetReadCount();
    for (i = 0; i < 3; i++) { /* LOST 后：不采样，输出保持零 */
        tick();
    }
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == reads_at_lost);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_lost_timeout_stops_chassis\n");
    return 0;
}

/* 恢复期内重获线 → 回 TRACKING，误差重新来自位图 */
static int test_reacquire_returns_tracking(void)
{
    LineFollow_Telemetry_T lt;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9));
    tick();
    FakeGrayPort_SetDarkChannels(0x0000u);
    tick();
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_RECOVERING);
    FakeGrayPort_SetDarkChannels(0x0060u); /* 线回到中央 */
    tick();
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_TRACKING);
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.error_mm, 0.0f, 1e-4f);
    printf("PASS: test_reacquire_returns_tracking\n");
    return 0;
}

/* 安全项：Stop 确定性 —— IDLE + 底盘刹车 + 目标清零 */
static int test_stop_is_deterministic(void)
{
    Chassis_Telemetry_T ct;

    setup(&k_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9));
    tick();
    LineFollow_Stop();
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_IDLE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    printf("PASS: test_stop_is_deterministic\n");
    return 0;
}

/* 外环门控：不足 10ms 的 Update 不采样灰度 */
static int test_gating_skips_sampling(void)
{
    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0060u);
    FakeClock_Advance(5u);
    LineFollow_Update();
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 0);
    FakeClock_Advance(5u);
    LineFollow_Update();
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 1);
    printf("PASS: test_gating_skips_sampling\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_start_gate_on_invalid_config();
    failures += test_idle_update_is_passive();
    failures += test_center_line_goes_straight();
    failures += test_diff_sign_line_right();
    failures += test_bit0_reversal_flips_side();
    failures += test_diff_limit_clamps();
    failures += test_lost_recovery_keeps_direction();
    failures += test_lost_timeout_stops_chassis();
    failures += test_reacquire_returns_tracking();
    failures += test_stop_is_deterministic();
    failures += test_gating_skips_sampling();

    if (failures != 0) {
        printf("%d line follow test(s) failed.\n", failures);
        return 1;
    }
    printf("All line follow tests passed.\n");
    return 0;
}
