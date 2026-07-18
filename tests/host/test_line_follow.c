/**
 * @file    test_line_follow.c
 * @brief   Host tests for the line-follow outer-loop service (S02 + S02b 深化).
 *
 * 组合：real line_follow + lost_line + chassis + gray + track_error + pid
 *       + speed_plan + track_elements + encoder + motor，只 fake 端口层
 *       （gray_port / board_gpio / motor_hw / clock）。
 * 关键口径：+误差 = 线在车右 → 左快右慢（left = base + c, right = base − c）；
 * 差速修正 c 经外环 Pid out_limit（= cfg.diff_limit_mps）限幅。
 * base 由 speed_plan 调制（S02b）——标准 k_cfg 用退化配置（straight=min、curve_error≤0）
 * 使 base 恒 0.5，保持既有差速/丢线断言逐字不变；速度调制另用 k_speed_cfg 验证。
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

/* 标准配置：pitch 10mm、bit0=左、差速限幅 0.6、回退 27mm、超时 100ms。
 * 速度规划退化（straight=min=0.5、curve_error=0）→ base 恒 0.5（既有断言保持）。
 * 元素检测启用四类：full_bar≥10 路、branch span≥6、confirm 2 拍。 */
static const LineFollow_Config_T k_cfg = {
    .pitch_mm = 10.0f,
    .bit0_is_left = true,
    .straight_speed_mps = 0.5f,
    .min_speed_mps = 0.5f,
    .curve_error_mm = 0.0f,       /* ≤0 退化：base 恒 straight=0.5 */
    .accel_mps_per_s = 10.0f,
    .decel_mps_per_s = 10.0f,
    .diff_limit_mps = 0.6f,
    .recovery_error_mm = 27.0f,
    .lost_timeout_ms = 100u,
    .full_bar_min_count = 10u,
    .branch_min_span = 6u,
    .element_confirm_ticks = 2u,
    .element_enable_mask = 0x000Fu, /* GAP|FULL_BAR|BRANCH_LEFT|BRANCH_RIGHT */
};

/* 速度调制配置：直道 0.8 / 入弯 0.3，curve_error 40mm，accel 10（0.1/拍）、decel 20（0.2/拍）。
 * 回退误差 50mm（≥curve_error）使丢线恢复期规划目标落到 min。 */
static const LineFollow_Config_T k_speed_cfg = {
    .pitch_mm = 10.0f,
    .bit0_is_left = true,
    .straight_speed_mps = 0.8f,
    .min_speed_mps = 0.3f,
    .curve_error_mm = 40.0f,
    .accel_mps_per_s = 10.0f,
    .decel_mps_per_s = 20.0f,
    .diff_limit_mps = 0.6f,
    .recovery_error_mm = 50.0f,
    .lost_timeout_ms = 100u,
    .full_bar_min_count = 10u,
    .branch_min_span = 6u,
    .element_confirm_ticks = 2u,
    .element_enable_mask = 0x000Fu,
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

/* 安全项：丢线超时 → LOST，底盘刹停（刹车真值表保持，方案 b）、目标清零、不再采样 */
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
    for (i = 0; i < 3; i++) { /* LOST 后：静默——不采样、不推进内环 */
        tick();
    }
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == reads_at_lost);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));   /* 刹车保持 */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_lost_timeout_stops_chassis\n");
    return 0;
}

/* F2 回归：外环积分器跨拍存活（积分限幅按误差口径显式给出，不再一拍饱和） */
static int test_outer_integral_accumulates(void)
{
    LineFollow_Telemetry_T lt1;
    LineFollow_Telemetry_T lt2;

    setup(&k_cfg);
    LineFollow_SetGains(0.0f, 0.001f, 0.0f); /* 纯积分 */
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9)); /* 恒定误差 +35mm */
    tick();
    LineFollow_GetTelemetry(&lt1);
    tick();
    LineFollow_GetTelemetry(&lt2);
    TEST_ASSERT_FLOAT_NEAR(lt1.diff_cmd_mps, 0.035f, 1e-5f);        /* ki×35×1拍 */
    TEST_ASSERT_FLOAT_NEAR(lt2.diff_cmd_mps, 2.0f * lt1.diff_cmd_mps, 1e-5f);
    printf("PASS: test_outer_integral_accumulates\n");
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

/* ======================== S02b 新增：M03 速度调制 ======================== */

/* 直道（误差 0）从 min 受 accel 斜坡逐拍提速朝 straight，单拍增量 = accel×elapsed */
static int test_speed_straight_accelerates(void)
{
    LineFollow_Telemetry_T lt;
    Chassis_Telemetry_T ct;

    setup(&k_speed_cfg);
    LineFollow_SetGains(0.0f, 0.0f, 0.0f); /* diff=0 → 双轮 = base */
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0060u); /* 中央线 error=0 → 目标 straight=0.8 */
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.4f, 1e-5f); /* 0.3 + accel(10)*0.01 */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.4f, 1e-5f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.4f, 1e-5f);
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.5f, 1e-5f); /* 再 +0.1 */
    printf("PASS: test_speed_straight_accelerates\n");
    return 0;
}

/* 提速到 straight 不过冲；入弯（|误差|≥curve_error）受 decel 斜坡朝 min 降速 */
static int test_speed_curve_decelerates(void)
{
    LineFollow_Telemetry_T lt;
    int i;

    setup(&k_speed_cfg);
    LineFollow_SetGains(0.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0060u);
    for (i = 0; i < 6; i++) { tick(); }    /* 0.3→0.8（5 拍到位，第 6 拍不过冲） */
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.8f, 1e-5f);
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 11)); /* pos11 误差 +55 ≥ curve_error → 目标 min */
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.6f, 1e-5f); /* 0.8 − decel(20)*0.01 */
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.4f, 1e-5f); /* 再 −0.2 */
    printf("PASS: test_speed_curve_decelerates\n");
    return 0;
}

/* 合成点消费的是 speed_plan 动态输出而非常量：left=base+diff, right=base−diff（base 为规划值） */
static int test_base_synthesis_uses_speed_plan(void)
{
    LineFollow_Telemetry_T lt;
    Chassis_Telemetry_T ct;

    setup(&k_speed_cfg);
    LineFollow_SetGains(0.01f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9)); /* pos9 误差 +35（<40） */
    tick();
    LineFollow_GetTelemetry(&lt);
    /* 目标 = 0.8 + (35/40)*(0.3−0.8) = 0.3625；current 0.3 与目标差 0.0625 ≤ accel0.1 → 到位 */
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.3625f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(lt.diff_cmd_mps, 0.35f, 1e-5f);
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.3625f + 0.35f, 1e-4f);
    TEST_ASSERT_FLOAT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.3625f - 0.35f, 1e-4f);
    printf("PASS: test_base_synthesis_uses_speed_plan\n");
    return 0;
}

/* Start 使规划基速复位 min（安全起步）：重启后不续用上次斜坡状态 */
static int test_start_resets_base_to_min(void)
{
    LineFollow_Telemetry_T lt;
    int i;

    setup(&k_speed_cfg);
    LineFollow_SetGains(0.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0060u);
    for (i = 0; i < 3; i++) { tick(); }    /* base 0.3→0.6 */
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.6f, 1e-5f);
    LineFollow_Stop();
    TEST_ASSERT_TRUE(LineFollow_Start());  /* speed_plan 复位 min=0.3 */
    tick();
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_FLOAT_NEAR(lt.base_speed_mps, 0.4f, 1e-5f); /* 0.3+0.1，非续 0.7 */
    printf("PASS: test_start_resets_base_to_min\n");
    return 0;
}

/* 丢线恢复期：大回退误差驱动规划基速朝 min 降速（安全慢行找线） */
static int test_recovery_decelerates_base(void)
{
    LineFollow_Telemetry_T lt;
    float base_before;
    int i;

    setup(&k_speed_cfg);
    LineFollow_SetGains(0.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0060u);
    for (i = 0; i < 5; i++) { tick(); }    /* base → 0.8 */
    FakeGrayPort_SetDarkChannels((uint16_t)(1u << 9)); /* 定方向 +，误差 35 */
    tick();
    LineFollow_GetTelemetry(&lt);
    base_before = lt.base_speed_mps;
    FakeGrayPort_SetDarkChannels(0x0000u); /* 丢线 → RECOVERING，回退误差 50≥40 → 目标 min */
    tick();
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_RECOVERING);
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_TRUE(lt.base_speed_mps < base_before); /* 朝 min 降速 */
    printf("PASS: test_recovery_decelerates_base\n");
    return 0;
}

/* ======================== S02b 新增：M02 元素事件面 ======================== */

/* 断线（全 0）连续 confirm_ticks 拍 → GAP 上升沿事件 */
static int test_element_gap_event(void)
{
    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0000u); /* 断线 */
    tick();                                 /* GAP count 1（未达阈） */
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == 0u);
    tick();                                 /* GAP count 2 → 确认+上升沿 */
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == LINE_FOLLOW_ELEM_GAP);
    printf("PASS: test_element_gap_event\n");
    return 0;
}

/* 横线（触双边且路数达阈）→ FULL_BAR 事件，且并列于正常 TRACKING（全黑=直行） */
static int test_element_full_bar_event(void)
{
    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0FFFu); /* 全 12 路：触双边、count 12≥10 */
    tick();
    tick();
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == LINE_FOLLOW_ELEM_FULL_BAR);
    TEST_ASSERT_TRUE(LineFollow_GetState() == LINE_FOLLOW_TRACKING);
    printf("PASS: test_element_full_bar_event\n");
    return 0;
}

/* 左岔：触左不触右、跨度达阈 → BRANCH_LEFT 事件 */
static int test_element_branch_left_event(void)
{
    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x007Fu); /* pos0..6：触左不触右、span 7≥6 */
    tick();
    tick();
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == LINE_FOLLOW_ELEM_BRANCH_LEFT);
    printf("PASS: test_element_branch_left_event\n");
    return 0;
}

/* 右岔：触右不触左、跨度达阈 → BRANCH_RIGHT 事件 */
static int test_element_branch_right_event(void)
{
    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0FE0u); /* pos5..11：触右不触左、span 7≥6 */
    tick();
    tick();
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == LINE_FOLLOW_ELEM_BRANCH_RIGHT);
    printf("PASS: test_element_branch_right_event\n");
    return 0;
}

/* 去毛刺：单拍出现的形态不足 confirm_ticks → 不确认、不出事件 */
static int test_element_debounce_single_tick(void)
{
    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0FFFu); /* FULL_BAR 谓词，仅 1 拍 */
    tick();
    FakeGrayPort_SetDarkChannels(0x0060u); /* 谓词不成立 → count 清 0 */
    tick();
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == 0u);
    printf("PASS: test_element_debounce_single_tick\n");
    return 0;
}

/* PollEvents 取后清（上升沿一次性），GetConfirmed 电平持续 */
static int test_element_poll_clears_level_persists(void)
{
    LineFollow_Telemetry_T lt;

    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0FFFu);
    tick();
    tick();
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == LINE_FOLLOW_ELEM_FULL_BAR);
    tick();                                 /* 仍 FULL_BAR，无新上升沿 */
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == 0u);
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_TRUE((lt.confirmed_elements & LINE_FOLLOW_ELEM_FULL_BAR) != 0u);
    printf("PASS: test_element_poll_clears_level_persists\n");
    return 0;
}

/* bit0_is_left 唯一修正点：同一 0x007F 在 false 下变成 BRANCH_RIGHT（无第二反转开关） */
static int test_element_bit0_reversal_swaps_branch(void)
{
    LineFollow_Config_T cfg = k_cfg;

    cfg.bit0_is_left = false;
    setup(&cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x007Fu); /* bit0..6；false → pos5..11 → 触右 */
    tick();
    tick();
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == LINE_FOLLOW_ELEM_BRANCH_RIGHT);
    printf("PASS: test_element_bit0_reversal_swaps_branch\n");
    return 0;
}

/* enable_mask=0：任何形态都不计数/不确认/不出事件 */
static int test_element_enable_mask_gates(void)
{
    LineFollow_Telemetry_T lt;
    LineFollow_Config_T cfg = k_cfg;
    int i;

    cfg.element_enable_mask = 0u;
    setup(&cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x0FFFu); /* 本会是 FULL_BAR */
    for (i = 0; i < 4; i++) { tick(); }
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == 0u);
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_TRUE(lt.confirmed_elements == 0u);
    printf("PASS: test_element_enable_mask_gates\n");
    return 0;
}

/* 元素检测与误差量化并列消费同一次读——驱动元素不新增采样点（每拍仍 1 次灰度读） */
static int test_element_no_second_sampling(void)
{
    int i;

    setup(&k_cfg);
    TEST_ASSERT_TRUE(LineFollow_Start());
    FakeGrayPort_SetDarkChannels(0x007Fu);
    for (i = 0; i < 3; i++) { tick(); }
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 3);
    printf("PASS: test_element_no_second_sampling\n");
    return 0;
}

/* IDLE 静默期：不采样故不驱动元素检测、不产生事件 */
static int test_idle_no_element_events(void)
{
    LineFollow_Telemetry_T lt;

    setup(&k_cfg); /* 未 Start → IDLE */
    FakeGrayPort_SetDarkChannels(0x0FFFu);
    tick();
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 0);
    TEST_ASSERT_TRUE(LineFollow_PollElementEvents() == 0u);
    LineFollow_GetTelemetry(&lt);
    TEST_ASSERT_TRUE(lt.confirmed_elements == 0u);
    printf("PASS: test_idle_no_element_events\n");
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
    failures += test_outer_integral_accumulates();
    failures += test_reacquire_returns_tracking();
    failures += test_stop_is_deterministic();
    failures += test_gating_skips_sampling();

    /* S02b：M03 速度调制 */
    failures += test_speed_straight_accelerates();
    failures += test_speed_curve_decelerates();
    failures += test_base_synthesis_uses_speed_plan();
    failures += test_start_resets_base_to_min();
    failures += test_recovery_decelerates_base();

    /* S02b：M02 元素事件面 */
    failures += test_element_gap_event();
    failures += test_element_full_bar_event();
    failures += test_element_branch_left_event();
    failures += test_element_branch_right_event();
    failures += test_element_debounce_single_tick();
    failures += test_element_poll_clears_level_persists();
    failures += test_element_bit0_reversal_swaps_branch();
    failures += test_element_enable_mask_gates();
    failures += test_element_no_second_sampling();
    failures += test_idle_no_element_events();

    if (failures != 0) {
        printf("%d line follow test(s) failed.\n", failures);
        return 1;
    }
    printf("All line follow tests passed.\n");
    return 0;
}
