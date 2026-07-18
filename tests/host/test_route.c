/**
 * @file    test_route.c
 * @brief   Host tests for the segmented-route execution service (S07, 计划表 §20).
 *
 * 链接组成（= line_follow 与 motion 两测试链接组成之并集，契约 §20.6）：真实
 * route + line_follow + lost_line + motion + chassis + odometry + heading + track_error
 * + track_elements + speed_plan + pid + encoder + motor + gray + imu + board_uart×4
 * + fake 端口（gray_port / board_gpio / motor_hw / uart_port / clock）。链 fake_clock、不链
 * fake_i2c_port（route 路径无 OLED/I2C，避免 Clock_NowMs 重定义）。只 fake 端口/硬件边界。
 *
 * 契约要点：route 每拍至多推进一个子服务；进入拍不驱动（隔拍刹停间隙）；进 motion 段前 catch-up；
 * 完成分派 = FOLLOW_UNTIL 元素事件 / motion 段 Motion_IsDone；段级 timeout_ms 是 route 唯一 liveness
 * 保护；任意态 Route_Stop 确定性刹停；IDLE/DONE/FAULT 静默。
 */
#include "app/service/route/route.h"

#include "app/service/chassis/chassis.h"
#include "app/service/line_follow/line_follow.h"   /* LINE_FOLLOW_ELEM_* 位号 */
#include "app/service/motion/motion.h"
#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/imu/imu.h"
#include "driver/motor/motor.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* imu.c 写寄存器路径（本测试从不调用）引用 Mspm0Runtime_DelayMs；空桩满足链接（同 test_motion）。 */
void Mspm0Runtime_DelayMs(uint32_t delay_ms) { (void)delay_ms; }

/* fake 注入/观测接口 */
extern void     FakeMotorHw_ResetLog(void);
extern int      FakeMotorHw_GetWriteCount(void);
extern bool     FakeMotorHw_IsBrakeActive(Motor_Id id);
extern void     FakeBoardGpio_SetRaw(int32_t left, int32_t right);
extern void     FakeClock_Set(uint32_t now_ms);
extern void     FakeClock_Advance(uint32_t delta_ms);
extern void     FakeGrayPort_Reset(void);
extern void     FakeGrayPort_SetDarkChannels(uint16_t channel_bitmap);
extern int      FakeGrayPort_GetReadCount(void);
extern void     FakeUartPort_ResetAll(void);
extern void     FakeUartPort_PushImuBytes(const uint8_t *data, uint32_t length);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_NEAR(actual, expected, eps) do { \
    if (fabsf((actual) - (expected)) > (eps)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (double)(actual), (double)(expected), (double)(eps), \
               __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* ---- 夹具 --------------------------------------------------------------- */

static uint32_t s_now;
static int32_t  s_raw_l;
static int32_t  s_raw_r;

/* motion 标定：1 脉冲=1mm、轮距100（R=200→内0.225/外0.375）。同 test_motion default。 */
static Motion_Config_T motion_cfg(void)
{
    Motion_Config_T c;
    c.mm_per_pulse = 1.0f;
    c.heading_sign = 1.0f;
    c.straight_speed_mps = 0.30f;
    c.turn_speed_mps = 0.30f;
    c.arc_speed_mps = 0.30f;
    c.track_width_mm = 100.0f;
    c.hold_kp = 0.01f;
    c.hold_ki = 0.0f;
    c.hold_kd = 0.0f;
    c.hold_diff_limit_mps = 0.15f;
    c.turn_kp = 0.02f;
    c.straight_tol_mm = 2.0f;
    c.turn_tol_deg = 2.0f;
    return c;
}

/* line_follow 标定：pitch10、bit0=左、差速限幅0.6、回退27、丢线超时100ms、元素 confirm 2 拍、四类启用。 */
static LineFollow_Config_T line_cfg(void)
{
    LineFollow_Config_T c;
    c.pitch_mm = 10.0f;
    c.bit0_is_left = true;
    c.straight_speed_mps = 0.5f;
    c.min_speed_mps = 0.5f;
    c.curve_error_mm = 0.0f;
    c.accel_mps_per_s = 10.0f;
    c.decel_mps_per_s = 10.0f;
    c.diff_limit_mps = 0.6f;
    c.recovery_error_mm = 27.0f;
    c.lost_timeout_ms = 100u;
    c.full_bar_min_count = 10u;
    c.branch_min_span = 6u;
    c.element_confirm_ticks = 2u;
    c.element_enable_mask = 0x000Fu;
    return c;
}

/* 标准装配序（与 SysInit 职责一致：Clock/Motor/Encoder/IMU/Chassis 先于 Service）。 */
static void setup(const Route_Segment_T *segs, uint8_t count)
{
    Motion_Config_T mc = motion_cfg();
    LineFollow_Config_T lc = line_cfg();

    Clock_Init();
    FakeClock_Set(1000u);
    s_now = 1000u;
    s_raw_l = 0;
    s_raw_r = 0;
    FakeBoardGpio_SetRaw(0, 0);
    FakeGrayPort_Reset();
    FakeUartPort_ResetAll();
    Motor_Init();
    Encoder_Init();
    Imu_Init();
    Chassis_Init();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, 100.0f, 0.0f, 0.0f);
    LineFollow_Init(&lc);
    Motion_Init(&mc);
    Route_Setup(segs, count);
    FakeMotorHw_ResetLog();
}

/* 编码器前进 pulses（两轮同量）：total[L]=-rawL、[R]=+rawR，故前进需 rawL 递减、rawR 递增。 */
static void drive_forward(int32_t pulses)
{
    s_raw_l -= pulses;
    s_raw_r += pulses;
    FakeBoardGpio_SetRaw(s_raw_l, s_raw_r);
}

/* 组一个校验和正确的 yaw 读帧推入 IMU 端口（同 test_motion）。 */
static void push_yaw(float deg)
{
    int32_t raw = (int32_t)lroundf(deg * 32768.0f / 180.0f);
    int16_t r16;
    uint8_t frame[5];

    if (raw > 32767) { raw = 32767; }
    if (raw < -32768) { raw = -32768; }
    r16 = (int16_t)raw;
    frame[0] = 0x5Au;
    frame[1] = 0xBBu;
    frame[2] = (uint8_t)((uint16_t)r16 & 0xFFu);
    frame[3] = (uint8_t)(((uint16_t)r16 >> 8) & 0xFFu);
    frame[4] = (uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]);
    FakeUartPort_PushImuBytes(frame, sizeof(frame));
}

/* 推进一个 10ms route 拍（now_ms 与 fake clock 同步）。 */
static void tick(void)
{
    s_now += 10u;
    FakeClock_Advance(10u);
    Route_Update(s_now);
}

/* ---- 生命周期 / 静默 ---------------------------------------------------- */

/* Setup 后（未 Start，state IDLE）Update 静默：不推子服务、零电机命令。 */
static int test_setup_before_start_is_silent(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    int i;

    setup(segs, 1u);
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_IDLE);
    for (i = 0; i < 5; i++) { tick(); }
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 0);
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_IDLE);
    printf("PASS: test_setup_before_start_is_silent\n");
    return 0;
}

/* 空表 Start → DONE + IsDone；Update 静默。 */
static int test_empty_table_start_is_done(void)
{
    setup(NULL, 0u);
    Route_Start();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_DONE);
    TEST_ASSERT_TRUE(Route_IsDone());
    tick();
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(Route_IsDone());
    printf("PASS: test_empty_table_start_is_done\n");
    return 0;
}

/* Start 本身不驱动；首个 Update 是进入拍，也不驱动（进入拍无底盘命令）。 */
static int test_start_and_enter_tick_do_not_drive(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };

    setup(segs, 1u);
    Route_Start();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_RUNNING);
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);   /* Start 不驱动 */
    tick();                                                /* 进入拍：catch-up + StartStraight，不驱动 */
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_RUNNING);
    printf("PASS: test_start_and_enter_tick_do_not_drive\n");
    return 0;
}

/* ---- FOLLOW_UNTIL 段 ---------------------------------------------------- */

/* 目标元素连续 confirm 拍 → 命中 → 段完成 + LineFollow 刹停 + 推进下一段。 */
static int test_follow_until_completes_and_advances(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
        { ROUTE_SEG_STRAIGHT,     0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    Route_Telemetry_T t;

    setup(segs, 2u);
    Route_Start();
    tick();                                        /* 进入 FOLLOW_UNTIL：LineFollow_Start */
    FakeGrayPort_SetDarkChannels(0x0FFFu);         /* 横线谓词 */
    tick();                                        /* FULL_BAR count 1，未确认 */
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_RUNNING);
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.segment_index == 0u);
    tick();                                        /* count 2 → 确认上升沿 → 段完成 → 推进段 1 */
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_RUNNING);
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.segment_index == 1u);
    TEST_ASSERT_TRUE(t.current_kind == ROUTE_SEG_STRAIGHT);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));   /* 完成拍刹停 */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_follow_until_completes_and_advances\n");
    return 0;
}

/* 目标未命中（喂中央线，无元素）→ 不推进，停留段 0。 */
static int test_follow_until_not_hit_stays(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    Route_Telemetry_T t;
    int i;

    setup(segs, 1u);
    Route_Start();
    tick();
    FakeGrayPort_SetDarkChannels(0x0060u);         /* 中央线，非 FULL_BAR */
    for (i = 0; i < 5; i++) { tick(); }
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_RUNNING);
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.segment_index == 0u);
    printf("PASS: test_follow_until_not_hit_stays\n");
    return 0;
}

/* OR 语义：until = GAP|BRANCH_LEFT，喂左岔 → 任一位命中即完成。 */
static int test_follow_until_or_semantics(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL,
          (uint16_t)(LINE_FOLLOW_ELEM_GAP | LINE_FOLLOW_ELEM_BRANCH_LEFT),
          0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
        { ROUTE_SEG_TURN, 0u, 0.0f, false, 90.0f, 0.0f, 0.0f, 0u },
    };
    Route_Telemetry_T t;

    setup(segs, 2u);
    Route_Start();
    tick();
    FakeGrayPort_SetDarkChannels(0x007Fu);         /* pos0..6：触左不触右、span7 → BRANCH_LEFT */
    tick();
    tick();                                        /* 确认 → BRANCH_LEFT 命中 OR 掩码 → 完成 */
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.segment_index == 1u);
    TEST_ASSERT_TRUE(t.current_kind == ROUTE_SEG_TURN);
    printf("PASS: test_follow_until_or_semantics\n");
    return 0;
}

/* line_follow 丢线超时 LOST → ROUTE_FAULT + 确定性刹停。 */
static int test_follow_until_lost_faults(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    int i;

    setup(segs, 1u);
    Route_Start();
    tick();                                        /* 进入：TRACKING */
    FakeGrayPort_SetDarkChannels(0x0000u);         /* 全丢线 → RECOVERING → 100ms 后 LOST */
    for (i = 0; i < 15; i++) {
        tick();
        if (Route_GetState() == ROUTE_FAULT) { break; }
    }
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_follow_until_lost_faults\n");
    return 0;
}

/* ---- motion 段 --------------------------------------------------------- */

/* STRAIGHT：注入编码器 → Motion_IsDone → DONE + 刹停。 */
static int test_straight_completes(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    int i;

    setup(segs, 1u);
    Route_Start();
    for (i = 0; i < 60; i++) {
        drive_forward(20);
        tick();
        if (Route_IsDone()) { break; }
    }
    TEST_ASSERT_TRUE(Route_IsDone());
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_DONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_straight_completes\n");
    return 0;
}

/* TURN：注入 IMU 航向 → Motion_IsDone → DONE。 */
static int test_turn_completes(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_TURN, 0u, 0.0f, false, 90.0f, 0.0f, 0.0f, 0u },
    };
    float yaw;
    int i;

    setup(segs, 1u);
    Route_Start();
    yaw = 0.0f;
    for (i = 0; i < 60; i++) {
        push_yaw(yaw);
        tick();
        if (Route_IsDone()) { break; }
        if (yaw < 90.0f) { yaw += 10.0f; }
    }
    TEST_ASSERT_TRUE(Route_IsDone());
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    printf("PASS: test_turn_completes\n");
    return 0;
}

/* ARC：注入编码器 + IMU 航向 → Motion_IsDone → DONE。 */
static int test_arc_completes(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_ARC, 0u, 0.0f, false, 0.0f, 200.0f, 90.0f, 0u },
    };
    float yaw;
    int i;

    setup(segs, 1u);
    Route_Start();
    yaw = 0.0f;
    for (i = 0; i < 60; i++) {
        drive_forward(40);
        push_yaw(yaw);
        tick();
        if (Route_IsDone()) { break; }
        if (yaw < 90.0f) { yaw += 10.0f; }
    }
    TEST_ASSERT_TRUE(Route_IsDone());
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_arc_completes\n");
    return 0;
}

/* 段参数非法（distance≤0）→ 段启动被拒 → FAULT + 静默。 */
static int test_straight_invalid_faults(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };

    setup(segs, 1u);
    Route_Start();
    tick();                                        /* 进入被拒 → abort_fault → FAULT + 确定性刹停 */
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));   /* 段失败恒 Chassis_Stop（§20.3） */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_straight_invalid_faults\n");
    return 0;
}

/* 段参数非法（turn_deg==0）→ FAULT。 */
static int test_turn_invalid_faults(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_TURN, 0u, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };

    setup(segs, 1u);
    Route_Start();
    tick();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    printf("PASS: test_turn_invalid_faults\n");
    return 0;
}

/* 段参数非法（R<track/2=50）→ FAULT。 */
static int test_arc_invalid_faults(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_ARC, 0u, 0.0f, false, 0.0f, 40.0f, 90.0f, 0u },
    };

    setup(segs, 1u);
    Route_Start();
    tick();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    printf("PASS: test_arc_invalid_faults\n");
    return 0;
}

/* ---- 进 motion 段 catch-up --------------------------------------------- */

/* FOLLOW_UNTIL（odometry 冻结）期注入 IMU 航向漂移 → 进 STRAIGHT heading_hold：
 * 进 motion 段前 catch-up 使 heading0 反映真实漂移航向，首个驱动拍不产生幻纠偏差速（左≈右）。 */
static int test_motion_catchup_no_phantom_correction(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
        { ROUTE_SEG_STRAIGHT,     0u, 1000.0f, true, 0.0f, 0.0f, 0.0f, 0u },
    };
    Chassis_Telemetry_T ct;
    Route_Telemetry_T rt;

    setup(segs, 2u);
    Route_Start();
    tick();                                        /* 进入 FOLLOW_UNTIL */
    FakeGrayPort_SetDarkChannels(0x0FFFu);         /* FULL_BAR 谓词 */
    push_yaw(30.0f); tick();                       /* 航向漂到 +30（IMU 帧堆积，follow 期不消费） */
    push_yaw(30.0f); tick();                       /* FULL_BAR 确认 → 段 0 完成 → 进段 1 待进入 */
    Route_GetTelemetry(&rt);
    TEST_ASSERT_TRUE(rt.segment_index == 1u);

    push_yaw(30.0f); tick();                        /* 进入 STRAIGHT：catch-up 排空 IMU → heading0=+30 */
    push_yaw(30.0f); tick();                        /* 首个驱动拍：当前航向 +30 = heading0 → err≈0 */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT],
                     ct.target_mps[CHASSIS_SIDE_RIGHT], 1e-4f);   /* 无幻纠偏 */
    printf("PASS: test_motion_catchup_no_phantom_correction\n");
    return 0;
}

/* ---- 段间确定性交接 ---------------------------------------------------- */

/* FOLLOW_UNTIL→TURN：段 0 完成拍刹停，隔一拍（进入拍）仍刹停不驱动，再下一拍才驱动段 1。 */
static int test_deterministic_handoff_brake_gap(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
        { ROUTE_SEG_TURN, 0u, 0.0f, false, 90.0f, 0.0f, 0.0f, 0u },
    };
    int writes_after_complete;

    setup(segs, 2u);
    Route_Start();
    tick();                                        /* 进入 FOLLOW_UNTIL */
    FakeGrayPort_SetDarkChannels(0x0FFFu);
    tick();                                         /* FULL_BAR count 1 */
    tick();                                         /* 完成段 0：LineFollow_Stop → 刹停 */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    writes_after_complete = FakeMotorHw_GetWriteCount();

    push_yaw(0.0f);
    tick();                                         /* 进入拍（段 1 TURN）：catch-up + StartTurn，不驱动 */
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == writes_after_complete); /* 进入拍无底盘命令 */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));                /* 刹停间隙保持 */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));

    push_yaw(5.0f);
    tick();                                         /* 段 1 首个驱动拍：motion 驱动，退出刹停 */
    TEST_ASSERT_TRUE(!FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    printf("PASS: test_deterministic_handoff_brake_gap\n");
    return 0;
}

/* ---- 每拍单子服务 / 无双泵 --------------------------------------------- */

/* FOLLOW_UNTIL 段推进期：灰度每拍读一次（line_follow 被推进）；motion 未被推进（注入 IMU 不被消费）。 */
static int test_single_subservice_follow_freezes_motion(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    Motion_Telemetry_T mt;

    setup(segs, 1u);
    Route_Start();
    tick();                                         /* 进入：Start，不采样 */
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 0);
    FakeGrayPort_SetDarkChannels(0x0060u);          /* 中央线，不完成 */
    push_yaw(45.0f);                                /* 注入 IMU 帧 */
    tick();                                         /* 推进 line_follow：灰度 +1 */
    tick();                                         /* 灰度 +1 */
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 2);
    Motion_GetTelemetry(&mt);
    TEST_ASSERT_NEAR(mt.heading_deg, 0.0f, 1e-3f);  /* motion 未被推进 → IMU 未消费、航向冻结 */
    printf("PASS: test_single_subservice_follow_freezes_motion\n");
    return 0;
}

/* motion 段推进期：灰度零采样（line_follow 未被推进）。 */
static int test_single_subservice_motion_no_gray(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    int i;

    setup(segs, 1u);
    Route_Start();
    for (i = 0; i < 5; i++) {
        drive_forward(10);
        tick();
    }
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == 0);   /* line_follow 从未被推进 */
    printf("PASS: test_single_subservice_motion_no_gray\n");
    return 0;
}

/* ---- 段级超时 ---------------------------------------------------------- */

/* timeout_ms>0 且段未完成 → 超时 → FAULT + 确定性刹停。 */
static int test_segment_timeout_faults(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100000.0f, false, 0.0f, 0.0f, 0.0f, 50u },  /* 目标极远、50ms 超时 */
    };
    int i;

    setup(segs, 1u);
    Route_Start();
    for (i = 0; i < 20; i++) {
        /* 不注入编码器 → 永不完成；50ms（进入拍后约 5 驱动拍）触发超时 */
        tick();
        if (Route_GetState() == ROUTE_FAULT) { break; }
    }
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_segment_timeout_faults\n");
    return 0;
}

/* timeout_ms==0：不触发超时，自然完成路径不受影响。 */
static int test_timeout_zero_completes_normally(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    int i;

    setup(segs, 1u);
    Route_Start();
    for (i = 0; i < 60; i++) {
        drive_forward(20);
        tick();
        if (Route_IsDone()) { break; }
    }
    TEST_ASSERT_TRUE(Route_IsDone());                /* timeout 0 未误触发 */
    TEST_ASSERT_TRUE(Route_GetState() != ROUTE_FAULT);
    printf("PASS: test_timeout_zero_completes_normally\n");
    return 0;
}

/* ---- Route_Stop ---------------------------------------------------------- */

/* RUNNING FOLLOW_UNTIL 中 Route_Stop → LineFollow 刹停 + IDLE + 此后静默。 */
static int test_stop_during_follow(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    int reads;
    int i;

    setup(segs, 1u);
    Route_Start();
    tick();
    FakeGrayPort_SetDarkChannels(0x0060u);
    tick();
    Route_Stop();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_IDLE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    reads = FakeGrayPort_GetReadCount();
    for (i = 0; i < 3; i++) { tick(); }             /* IDLE 静默：不再采样 */
    TEST_ASSERT_TRUE(FakeGrayPort_GetReadCount() == reads);
    printf("PASS: test_stop_during_follow\n");
    return 0;
}

/* RUNNING motion 段中 Route_Stop → Motion 刹停 + IDLE。 */
static int test_stop_during_motion(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100000.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };

    setup(segs, 1u);
    Route_Start();
    drive_forward(20); tick();
    drive_forward(20); tick();
    Route_Stop();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_IDLE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_stop_during_motion\n");
    return 0;
}

/* ---- FAULT 静默 + 遥测 -------------------------------------------------- */

/* FAULT 后 Update 静默：电机命令不刷新、刹车保持。 */
static int test_fault_state_is_silent(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },   /* 立即被拒 → FAULT */
    };
    int writes;
    int i;

    setup(segs, 1u);
    Route_Start();
    tick();
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    writes = FakeMotorHw_GetWriteCount();
    for (i = 0; i < 5; i++) {
        drive_forward(20);
        tick();
    }
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == writes);   /* 静默 */
    TEST_ASSERT_TRUE(Route_GetState() == ROUTE_FAULT);
    printf("PASS: test_fault_state_is_silent\n");
    return 0;
}

/* 遥测：state/segment_index/segment_count/current_kind 随执行一致。 */
static int test_telemetry_reflects_progress(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_FOLLOW_UNTIL, LINE_FOLLOW_ELEM_FULL_BAR, 0.0f, false, 0.0f, 0.0f, 0.0f, 0u },
        { ROUTE_SEG_STRAIGHT,     0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };
    Route_Telemetry_T t;
    int i;

    setup(segs, 2u);
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == ROUTE_IDLE);
    TEST_ASSERT_TRUE(t.segment_count == 2u);
    TEST_ASSERT_TRUE(t.segment_index == 0u);
    TEST_ASSERT_TRUE(t.current_kind == ROUTE_SEG_FOLLOW_UNTIL);

    Route_Start();
    tick();
    FakeGrayPort_SetDarkChannels(0x0FFFu);
    tick();
    tick();                                          /* 段 0 完成 → 段 1 */
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == ROUTE_RUNNING);
    TEST_ASSERT_TRUE(t.segment_index == 1u);
    TEST_ASSERT_TRUE(t.current_kind == ROUTE_SEG_STRAIGHT);

    for (i = 0; i < 60; i++) {
        drive_forward(20);
        tick();
        if (Route_IsDone()) { break; }
    }
    Route_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == ROUTE_DONE);
    TEST_ASSERT_TRUE(t.segment_index == 1u);         /* DONE：报告最后处理段 */
    printf("PASS: test_telemetry_reflects_progress\n");
    return 0;
}

/* GetTelemetry(NULL) 安全。 */
static int test_telemetry_null_safe(void)
{
    static const Route_Segment_T segs[] = {
        { ROUTE_SEG_STRAIGHT, 0u, 100.0f, false, 0.0f, 0.0f, 0.0f, 0u },
    };

    setup(segs, 1u);
    Route_GetTelemetry(NULL);
    TEST_ASSERT_TRUE(true);
    printf("PASS: test_telemetry_null_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_setup_before_start_is_silent();
    failures += test_empty_table_start_is_done();
    failures += test_start_and_enter_tick_do_not_drive();
    failures += test_follow_until_completes_and_advances();
    failures += test_follow_until_not_hit_stays();
    failures += test_follow_until_or_semantics();
    failures += test_follow_until_lost_faults();
    failures += test_straight_completes();
    failures += test_turn_completes();
    failures += test_arc_completes();
    failures += test_straight_invalid_faults();
    failures += test_turn_invalid_faults();
    failures += test_arc_invalid_faults();
    failures += test_motion_catchup_no_phantom_correction();
    failures += test_deterministic_handoff_brake_gap();
    failures += test_single_subservice_follow_freezes_motion();
    failures += test_single_subservice_motion_no_gray();
    failures += test_segment_timeout_faults();
    failures += test_timeout_zero_completes_normally();
    failures += test_stop_during_follow();
    failures += test_stop_during_motion();
    failures += test_fault_state_is_silent();
    failures += test_telemetry_reflects_progress();
    failures += test_telemetry_null_safe();

    if (failures != 0) {
        printf("%d route test(s) failed.\n", failures);
        return 1;
    }
    printf("All route tests passed.\n");
    return 0;
}
