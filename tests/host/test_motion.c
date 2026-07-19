/**
 * @file    test_motion.c
 * @brief   Host tests for the semantic-motion service (S06).
 *
 * 契约回顾（phase4 计划表 §15）：
 * - Motion_Update 每拍：Imu_Update()（本服务独占排空）→ 读 Encoder/IMU 快照 → 以 total_pulses
 *   差值一次性消费推进 odometry → 取位姿 → STRAIGHT/TURN 算控制律 → Chassis_SetTargetMps →
 *   到位则 Chassis_Stop+DONE；末尾恒推进 Chassis_Update()；IDLE/DONE 底盘静默（刹车真值表保持）。
 * - 里程计一次性消费：Motion_Update 调用快于底盘采样时，total 不变 → 不二次前进（无双计数）。
 * - 单一所有者：脉冲→距离/yaw 符号在 odometry cfg；限幅/刹车在 motor/chassis；motion 不复做。
 *
 * 链接组成：真实 motion+chassis+odometry+heading+encoder+motor+pid+imu+board_uart
 * + fake_board_gpio（编码器原始计数注入）+ fake_motor_hw（电机抓取）
 * + fake_uart_port（IMU 帧注入）+ fake_clock（时间注入）。不链 fake_i2c_port（Clock_NowMs 冲突）。
 */
#include "app/service/motion/motion.h"

#include "app/service/chassis/chassis.h"
#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/imu/imu.h"
#include "driver/motor/motor.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* imu.c 的写寄存器路径（Imu_ZeroYaw/SetOutputRate，本测试从不调用）引用
 * Mspm0Runtime_DelayMs；提供空桩满足链接（同 test_imu.c 做法，Clock_NowMs 走 fake_clock）。 */
void Mspm0Runtime_DelayMs(uint32_t delay_ms)
{
    (void)delay_ms;
}

/* fake 注入/观测接口 */
extern void FakeMotorHw_ResetLog(void);
extern int  FakeMotorHw_GetWriteCount(void);
extern bool FakeMotorHw_IsBrakeActive(Motor_Id id);
extern void FakeBoardGpio_SetRaw(int32_t left, int32_t right);
extern void FakeClock_Set(uint32_t now_ms);
extern void FakeClock_Advance(uint32_t delta_ms);
extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_PushImuBytes(const uint8_t *data, uint32_t length);

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

/* 测试侧原始编码器计数轨迹。total_pulses[L]=-rawL、[R]=+rawR（encoder.c 方向修正），
 * 故前进（两轮正 total）需 rawL 递减、rawR 递增。 */
static int32_t s_raw_l;
static int32_t s_raw_r;

static Motion_Config_T default_cfg(void)
{
    Motion_Config_T cfg;
    cfg.mm_per_pulse = 1.0f;       /* 1 脉冲 = 1 mm，断言直观 */
    cfg.heading_sign = 1.0f;
    cfg.straight_speed_mps = 0.30f;
    /* 梯形剖面：匀速 0.60、起步 0.15、加速=减速 0.5 m/s^2（断言直观，mm_per_pulse=1）。 */
    cfg.profile_cruise_mps = 0.60f;
    cfg.profile_start_mps = 0.15f;
    cfg.profile_accel_mps2 = 0.5f;
    cfg.profile_decel_mps2 = 0.5f;
    cfg.profile_timeout_ticks = 0u;   /* 默认禁用看门狗：既有 profiled 用例不受影响 */
    cfg.turn_speed_mps = 0.30f;
    cfg.arc_speed_mps = 0.30f;
    cfg.track_width_mm = 100.0f;   /* R=200 → half=50：inner=0.225、outer=0.375（断言直观） */
    cfg.hold_kp = 0.01f;
    cfg.hold_ki = 0.0f;
    cfg.hold_kd = 0.0f;
    cfg.hold_diff_limit_mps = 0.15f;
    cfg.turn_kp = 0.02f;
    cfg.straight_tol_mm = 2.0f;
    cfg.turn_tol_deg = 2.0f;
    return cfg;
}

/* 标准装配序（与 SysInit 职责一致：Clock/Motor/Encoder/IMU 先于 Service）。 */
static void setup(void)
{
    Motion_Config_T cfg = default_cfg();

    Clock_Init();
    FakeClock_Set(1000u);
    s_raw_l = 0;
    s_raw_r = 0;
    FakeBoardGpio_SetRaw(0, 0);
    FakeUartPort_ResetAll();
    Motor_Init();
    Encoder_Init();
    Imu_Init();
    Chassis_Init();
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, 100.0f, 0.0f, 0.0f);
    FakeMotorHw_ResetLog();
    Motion_Init(&cfg);
}

/* 让编码器前进 pulses 个（两轮同量）；下一次 Encoder_Update 会读到。 */
static void drive_forward(int32_t pulses)
{
    s_raw_l -= pulses;
    s_raw_r += pulses;
    FakeBoardGpio_SetRaw(s_raw_l, s_raw_r);
}

/* 组一个校验和正确的 yaw 读帧推入 IMU 端口（0x5A 0xBB lo hi cksum，yaw=raw*180/32768）。 */
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

/* 推进一个 10ms 运动拍（可选先注入 yaw / 编码器增量由调用者预先 drive_forward）。 */
static void tick(void)
{
    FakeClock_Advance(10u);
    Motion_Update();
}

/* ---- 用例 --------------------------------------------------------------- */

/* 安全项：Init 静默（零电机命令）+ 初始 IDLE + 未完成。 */
static int test_init_is_silent_and_idle(void)
{
    setup();
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(Motion_IsDone() == false);
    printf("PASS: test_init_is_silent_and_idle\n");
    return 0;
}

/* StartStraight 参数校验：<=0 拒绝、>0 进入 STRAIGHT。 */
static int test_start_straight_rejects_nonpositive(void)
{
    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(0.0f, false) == false);
    TEST_ASSERT_TRUE(Motion_StartStraight(-5.0f, true) == false);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(Motion_StartStraight(100.0f, false) == true);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_STRAIGHT);
    printf("PASS: test_start_straight_rejects_nonpositive\n");
    return 0;
}

/* 直行推进：注入编码器增量 → 位姿前进（x 增，一次一拍延迟后累计）。 */
static int test_straight_advances_pose_forward(void)
{
    Motion_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(1000.0f, false));
    /* 每拍 +50 脉冲=50mm；因一拍采样延迟，多推几拍后位姿必然为正 */
    drive_forward(50); tick();
    drive_forward(50); tick();
    drive_forward(50); tick();
    Motion_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == MOTION_STRAIGHT);
    TEST_ASSERT_TRUE(t.x_mm > 0.0f);
    TEST_ASSERT_TRUE(t.progress > 0.0f);
    printf("PASS: test_straight_advances_pose_forward\n");
    return 0;
}

/* 直行到位：dist≥target → Chassis_Stop（刹车）+ DONE + IsDone。 */
static int test_straight_completes_and_brakes(void)
{
    int i;
    Motion_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(100.0f, false));
    for (i = 0; i < 60; i++) {
        drive_forward(10);
        tick();
        if (Motion_IsDone()) {
            break;
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Motion_GetTelemetry(&t);
    /* 到位量应 ≥ 目标（判据是 dist≥target） */
    TEST_ASSERT_TRUE(t.x_mm >= 100.0f - 1e-3f);
    printf("PASS: test_straight_completes_and_brakes\n");
    return 0;
}

/* DONE 静默：完成后多拍 Update 不再泵内环，刹车真值表保持、电机命令不刷新。 */
static int test_done_is_silent_brake_persists(void)
{
    int i;
    int writes_at_done;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(100.0f, false));
    for (i = 0; i < 60; i++) {
        drive_forward(10);
        tick();
        if (Motion_IsDone()) {
            break;
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    writes_at_done = FakeMotorHw_GetWriteCount();

    for (i = 0; i < 5; i++) {
        drive_forward(10);   /* 即使有新增量，DONE 也不消费/不驱动 */
        tick();
    }
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == writes_at_done);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    printf("PASS: test_done_is_silent_brake_persists\n");
    return 0;
}

/* IDLE 静默：Init 后多拍 Update 无电机命令（不泵内环）。 */
static int test_idle_update_is_silent(void)
{
    int i;

    setup();
    for (i = 0; i < 5; i++) {
        tick();
    }
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    printf("PASS: test_idle_update_is_silent\n");
    return 0;
}

/* 航向保持：rel>0（起点后偏 CCW/左）→ 左快右慢（CW 修正）。 */
static int test_heading_hold_corrects_ccw_drift(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(1000.0f, true));
    push_yaw(0.0f);  tick();   /* 播种航向=0（=基准） */
    push_yaw(10.0f); tick();   /* 漂到 +10° CCW */
    Chassis_GetTelemetry(&ct);
    /* 左快右慢：纠偏方向正确 */
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] >
                     ct.target_mps[CHASSIS_SIDE_RIGHT] + 1e-4f);
    printf("PASS: test_heading_hold_corrects_ccw_drift\n");
    return 0;
}

/* 航向保持对称性：rel<0（偏 CW/右）→ 右快左慢。 */
static int test_heading_hold_corrects_cw_drift(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(1000.0f, true));
    push_yaw(0.0f);   tick();
    push_yaw(-10.0f); tick();
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_RIGHT] >
                     ct.target_mps[CHASSIS_SIDE_LEFT] + 1e-4f);
    printf("PASS: test_heading_hold_corrects_cw_drift\n");
    return 0;
}

/* IMU 无效：heading_hold 开但无有效 IMU → corr=0（左右等速），仍按编码器测距前进。 */
static int test_invalid_imu_no_steer_but_measures(void)
{
    Chassis_Telemetry_T ct;
    Motion_Telemetry_T mt;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(1000.0f, true));
    /* 不注入任何 IMU 帧 → valid 恒 false */
    drive_forward(50); tick();
    drive_forward(50); tick();
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT],
                     ct.target_mps[CHASSIS_SIDE_RIGHT], 1e-6f);
    Motion_GetTelemetry(&mt);
    TEST_ASSERT_TRUE(mt.progress > 0.0f);   /* 陈旧航向下仍测距前进 */
    printf("PASS: test_invalid_imu_no_steer_but_measures\n");
    return 0;
}

/* StartTurn 参数校验：0 拒绝。 */
static int test_start_turn_rejects_zero(void)
{
    setup();
    TEST_ASSERT_TRUE(Motion_StartTurn(0.0f) == false);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(Motion_StartTurn(90.0f) == true);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_TURN);
    printf("PASS: test_start_turn_rejects_zero\n");
    return 0;
}

/* 定角左转（+）：原地转，左轮负、右轮正（CCW）。 */
static int test_turn_ccw_drives_left_neg_right_pos(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartTurn(90.0f));
    push_yaw(0.0f);  tick();
    push_yaw(20.0f); tick();   /* 仍在转（离 90 远） */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] < 0.0f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_RIGHT] > 0.0f);
    printf("PASS: test_turn_ccw_drives_left_neg_right_pos\n");
    return 0;
}

/* 定角右转（−）：左轮正、右轮负（CW），方向反转。 */
static int test_turn_cw_reverses_sign(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartTurn(-90.0f));
    push_yaw(0.0f);   tick();
    push_yaw(-20.0f); tick();
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] > 0.0f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_RIGHT] < 0.0f);
    printf("PASS: test_turn_cw_reverses_sign\n");
    return 0;
}

/* 转到位：航向达目标容差内 → Chassis_Stop（刹车）+ DONE。 */
static int test_turn_completes_at_tolerance(void)
{
    int i;
    float yaw;
    Motion_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Motion_StartTurn(90.0f));
    yaw = 0.0f;
    for (i = 0; i < 60; i++) {
        push_yaw(yaw);
        tick();
        if (Motion_IsDone()) {
            break;
        }
        if (yaw < 90.0f) {
            yaw += 10.0f;   /* 逼近目标 */
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Motion_GetTelemetry(&t);
    TEST_ASSERT_NEAR(t.heading_deg, 90.0f, 5.0f);
    printf("PASS: test_turn_completes_at_tolerance\n");
    return 0;
}

/* 里程计一次性消费：Update 快于底盘采样时，total 不变 → 位姿不二次前进（无双计数）。 */
static int test_odometry_consumed_once(void)
{
    Motion_Telemetry_T t1;
    Motion_Telemetry_T t2;

    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(1000.0f, false));
    drive_forward(100); tick();   /* 底盘采样→total=100 */
    tick();                       /* 消费 d=100 → x≈100 */
    Motion_GetTelemetry(&t1);
    TEST_ASSERT_TRUE(t1.x_mm > 0.0f);

    /* 不推进时钟、不改 raw → 底盘门控不到期、total 不变 */
    Motion_Update();
    Motion_GetTelemetry(&t2);
    TEST_ASSERT_NEAR(t2.x_mm, t1.x_mm, 1e-6f);  /* 未二次前进 */
    printf("PASS: test_odometry_consumed_once\n");
    return 0;
}

/* Motion_Stop：任意态 → Chassis_Stop（刹车）+ IDLE。 */
static int test_stop_from_any_state_idles_and_brakes(void)
{
    setup();
    TEST_ASSERT_TRUE(Motion_StartStraight(1000.0f, false));
    drive_forward(50); tick();
    Motion_Stop();
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_stop_from_any_state_idles_and_brakes\n");
    return 0;
}

/* IMU 独占路径：注入 yaw 帧经真实 imu.c 解析 → valid 翻真喂 odometry → 位姿航向反映。 */
static int test_imu_frame_parsed_into_pose_heading(void)
{
    Motion_Telemetry_T t;

    setup();
    push_yaw(45.0f);
    tick();   /* IDLE 态也推进里程计：Imu_Update 解析 → 航向播种 45° */
    Motion_GetTelemetry(&t);
    TEST_ASSERT_NEAR(t.heading_deg, 45.0f, 0.5f);
    printf("PASS: test_imu_frame_parsed_into_pose_heading\n");
    return 0;
}

/* 遥测一致性：state/target/progress 随原语反映；IDLE 时 target/progress 归零。 */
static int test_telemetry_reflects_state(void)
{
    Motion_Telemetry_T t;

    setup();
    Motion_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == MOTION_IDLE);
    TEST_ASSERT_NEAR(t.target, 0.0f, 1e-6f);
    TEST_ASSERT_NEAR(t.progress, 0.0f, 1e-6f);

    TEST_ASSERT_TRUE(Motion_StartStraight(250.0f, false));
    Motion_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == MOTION_STRAIGHT);
    TEST_ASSERT_NEAR(t.target, 250.0f, 1e-6f);

    TEST_ASSERT_TRUE(Motion_StartTurn(60.0f));
    Motion_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == MOTION_TURN);
    TEST_ASSERT_NEAR(t.target, 60.0f, 1e-6f);
    printf("PASS: test_telemetry_reflects_state\n");
    return 0;
}

/* ---- 定长梯形剖面直行原语（S06c，计划表 §27）---------------------------- */

/* StartProfiledStraight 参数校验：<=0 拒绝并保持前态、>0 进入 PROFILED_STRAIGHT。 */
static int test_start_profiled_rejects_nonpositive(void)
{
    setup();
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(0.0f, false) == false);
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(-5.0f, true) == false);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(1000.0f, false) == true);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_PROFILED_STRAIGHT);
    printf("PASS: test_start_profiled_rejects_nonpositive\n");
    return 0;
}

/* 起步段：dist≈0 → 前馈基速 = 剖面起步速 profile_start_mps(0.15)，远低于匀速上限。 */
static int test_profiled_starts_at_start_speed(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(2000.0f, false));
    tick();   /* 首拍播种、dist=0 → base=start=0.15 */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.15f, 1e-3f);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.15f, 1e-3f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] < 0.60f);   /* 未到匀速 */
    printf("PASS: test_profiled_starts_at_start_speed\n");
    return 0;
}

/* 匀速段：dist 进入平台区（337.5mm≤dist≤1640mm）→ 前馈基速夹到匀速上限 0.60。 */
static int test_profiled_reaches_cruise_midway(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(2000.0f, false));
    drive_forward(700);   /* 一次大位移 */
    tick();               /* 底盘采样→total=700 */
    tick();               /* 消费 d=700 → dist≈700（平台区）→ base=cruise */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.60f, 1e-3f);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.60f, 1e-3f);
    printf("PASS: test_profiled_reaches_cruise_midway\n");
    return 0;
}

/* 减速段：dist 逼近目标（剩余 100mm）→ 前馈 = sqrt(2*decel*0.1)=0.3162，低于匀速上限。 */
static int test_profiled_decelerates_near_target(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(2000.0f, false));
    drive_forward(1900);
    tick();               /* 采样→total=1900 */
    tick();               /* 消费 → dist≈1900，rem=100mm → 减速段 */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.316228f, 2e-3f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] < 0.60f);   /* 已降速 */
    printf("PASS: test_profiled_decelerates_near_target\n");
    return 0;
}

/* 到位：dist≥target → Chassis_Stop（刹车）+ DONE + IsDone（同恒速直行到位判据）。 */
static int test_profiled_completes_and_brakes(void)
{
    int i;
    Motion_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(2000.0f, false));
    for (i = 0; i < 60; i++) {
        drive_forward(100);
        tick();
        if (Motion_IsDone()) {
            break;
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Motion_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.x_mm >= 2000.0f - 1e-3f);
    printf("PASS: test_profiled_completes_and_brakes\n");
    return 0;
}

/* 航向保持仍生效（横向正交）：rel>0（偏 CCW/左）→ 左快右慢；且前馈基速非零（剖面驱动）。 */
static int test_profiled_heading_hold_corrects_ccw(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(1000.0f, true));
    push_yaw(0.0f);  tick();   /* 播种航向=0=基准 */
    push_yaw(10.0f); tick();   /* 漂到 +10° CCW，dist≈0 → base=start */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] >
                     ct.target_mps[CHASSIS_SIDE_RIGHT] + 1e-4f);
    printf("PASS: test_profiled_heading_hold_corrects_ccw\n");
    return 0;
}

/* 剖面参数运行时读写（§28）：Set→Get 往返一致。 */
static int test_profile_params_set_get_roundtrip(void)
{
    float cruise, start, accel, decel;

    setup();
    Motion_SetProfileParams(0.55f, 0.11f, 0.44f, 0.66f);
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    TEST_ASSERT_NEAR(cruise, 0.55f, 1e-6f);
    TEST_ASSERT_NEAR(start, 0.11f, 1e-6f);
    TEST_ASSERT_NEAR(accel, 0.44f, 1e-6f);
    TEST_ASSERT_NEAR(decel, 0.66f, 1e-6f);
    printf("PASS: test_profile_params_set_get_roundtrip\n");
    return 0;
}

/* 运行时改剖面 → 定长直行即时采用新匀速上限（默认 cruise=0.60，改成 0.40 后平台区 base=0.40）。 */
static int test_profile_params_apply_to_run(void)
{
    Chassis_Telemetry_T ct;

    setup();
    Motion_SetProfileParams(0.40f, 0.15f, 0.5f, 0.5f);   /* 匀速上限改 0.40 */
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(2000.0f, false));
    drive_forward(700);
    tick();
    tick();   /* dist≈700 平台区 → base 夹到新 cruise=0.40 */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.40f, 1e-3f);
    printf("PASS: test_profile_params_apply_to_run\n");
    return 0;
}

/* §8.1 防跑飞看门狗：编码器脱线(dist 恒≈0，永不达标) → base=start 一直驱动 →
 * profile_timeout_ticks 拍后确定性 Chassis_Stop + DONE（防跑飞）。 */
static int test_profiled_watchdog_stops_runaway(void)
{
    Motion_Config_T cfg = default_cfg();
    Chassis_Telemetry_T ct;
    bool drove_before_timeout = false;
    int i;
    int done_at = 99;

    setup();
    cfg.profile_timeout_ticks = 20u;   /* 20 拍运行上限 */
    Motion_Init(&cfg);
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(5000.0f, false));   /* 远目标 */

    /* 不注入编码器增量 → dist 恒≈0 → dist>=target 永不满足（模拟编码器脱线跑飞）。 */
    for (i = 0; i < 30; i++) {
        tick();
        if (i < 10) {
            Chassis_GetTelemetry(&ct);
            if (ct.target_mps[CHASSIS_SIDE_LEFT] > 0.0f) {
                drove_before_timeout = true;   /* 看门狗前确有驱动（跑飞真实存在） */
            }
        }
        if (Motion_IsDone()) {
            done_at = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(drove_before_timeout);
    TEST_ASSERT_TRUE(Motion_IsDone());
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));    /* Chassis_Stop 刹车 */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    TEST_ASSERT_TRUE(done_at < 25);   /* ≈20 拍触发，远早于 30 */
    printf("PASS: test_profiled_watchdog_stops_runaway\n");
    return 0;
}

/* 看门狗禁用（profile_timeout_ticks=0，default_cfg 口径）：正常到位仍靠 dist>=target。 */
static int test_profiled_watchdog_disabled_by_default(void)
{
    int i;

    setup();   /* default_cfg: profile_timeout_ticks=0 */
    TEST_ASSERT_TRUE(Motion_StartProfiledStraight(2000.0f, false));
    /* 编码器正常推进 → 正常到位 DONE（看门狗禁用不干扰）。 */
    for (i = 0; i < 60; i++) {
        drive_forward(100);
        tick();
        if (Motion_IsDone()) {
            break;
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    printf("PASS: test_profiled_watchdog_disabled_by_default\n");
    return 0;
}

/* GetProfileParams NULL 安全。 */
static int test_get_profile_params_null_safe(void)
{
    float cruise = -1.0f;

    setup();
    Motion_GetProfileParams(NULL, NULL, NULL, NULL);   /* 不崩、无副作用 */
    Motion_GetProfileParams(&cruise, NULL, NULL, NULL); /* 任一 NULL → 无副作用（cruise 不被写） */
    TEST_ASSERT_NEAR(cruise, -1.0f, 1e-6f);
    printf("PASS: test_get_profile_params_null_safe\n");
    return 0;
}

/* ---- 圆弧原语（S06b，计划表 §19）---------------------------------------- */

/* StartArc 参数校验：R≤0 / arc_deg=0 / R<track/2 拒绝并保持前态；合法→ARC。 */
static int test_start_arc_rejects_invalid(void)
{
    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(0.0f, 90.0f) == false);
    TEST_ASSERT_TRUE(Motion_StartArc(-10.0f, 90.0f) == false);
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 0.0f) == false);
    TEST_ASSERT_TRUE(Motion_StartArc(40.0f, 90.0f) == false);   /* R=40 < track/2=50 → 内轮反向 */
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f) == true);
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_ARC);
    printf("PASS: test_start_arc_rejects_invalid\n");
    return 0;
}

/* 前馈速度比（CCW）：起步 arc_len≈0、rel=0 → corr=0 → 纯前馈；
 * R=200/half=50/vc=0.30 → 左(内)=0.225、右(外)=0.375。 */
static int test_arc_feedforward_ratio_ccw(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    tick();   /* 无编码器增量、无 IMU → arc_len=0、rel=0、corr=0 */
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.225f, 1e-3f);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.375f, 1e-3f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_RIGHT] >
                     ct.target_mps[CHASSIS_SIDE_LEFT]);
    printf("PASS: test_arc_feedforward_ratio_ccw\n");
    return 0;
}

/* 前馈速度比（CW）：左右互换 → 左(外)=0.375、右(内)=0.225。 */
static int test_arc_feedforward_ratio_cw(void)
{
    Chassis_Telemetry_T ct;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, -90.0f));
    tick();
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_LEFT], 0.375f, 1e-3f);
    TEST_ASSERT_NEAR(ct.target_mps[CHASSIS_SIDE_RIGHT], 0.225f, 1e-3f);
    printf("PASS: test_arc_feedforward_ratio_cw\n");
    return 0;
}

/* 航向修正（CCW 欠转）：注入编码器位移使 arc_len 增、但 IMU 航向held 0（rel=0）→
 * 期望已转角 exp>0、误差>0 → corr>0 → 右轮超前馈外轮、左轮低于前馈内轮（转更多）。 */
static int test_arc_correction_underturn_ccw(void)
{
    Chassis_Telemetry_T ct;
    int i;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    push_yaw(0.0f);   /* 播种航向=0=基准 */
    for (i = 0; i < 5; i++) {
        drive_forward(40);   /* 沿航向 0 前进 → x 增、arc_len 增 */
        push_yaw(0.0f);      /* 航向保持 0 → rel=0（欠转） */
        tick();
    }
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_RIGHT] > 0.375f + 1e-3f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] < 0.225f - 1e-3f);
    printf("PASS: test_arc_correction_underturn_ccw\n");
    return 0;
}

/* 航向修正（CW 欠转）：对称——左轮超前馈外轮、右轮低于前馈内轮。 */
static int test_arc_correction_underturn_cw(void)
{
    Chassis_Telemetry_T ct;
    int i;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, -90.0f));
    push_yaw(0.0f);
    for (i = 0; i < 5; i++) {
        drive_forward(40);
        push_yaw(0.0f);
        tick();
    }
    Chassis_GetTelemetry(&ct);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_LEFT] > 0.375f + 1e-3f);
    TEST_ASSERT_TRUE(ct.target_mps[CHASSIS_SIDE_RIGHT] < 0.225f - 1e-3f);
    printf("PASS: test_arc_correction_underturn_cw\n");
    return 0;
}

/* 完成（航向权威）：航向扫到 arc_deg → |arc_deg|−|rel|≤tol → Chassis_Stop（刹车）+ DONE。 */
static int test_arc_completes_by_heading_and_brakes(void)
{
    int i;
    float yaw;
    Motion_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    yaw = 0.0f;
    for (i = 0; i < 60; i++) {
        drive_forward(40);
        push_yaw(yaw);
        tick();
        if (Motion_IsDone()) {
            break;
        }
        if (yaw < 90.0f) {
            yaw += 10.0f;
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Motion_GetTelemetry(&t);
    TEST_ASSERT_NEAR(t.heading_deg, 90.0f, 5.0f);
    printf("PASS: test_arc_completes_by_heading_and_brakes\n");
    return 0;
}

/* ARC-DONE 静默：完成后多拍 Update 不再泵内环，刹车真值表保持、电机命令不刷新。 */
static int test_arc_done_is_silent(void)
{
    int i;
    float yaw;
    int writes_at_done;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    yaw = 0.0f;
    for (i = 0; i < 60; i++) {
        drive_forward(40);
        push_yaw(yaw);
        tick();
        if (Motion_IsDone()) {
            break;
        }
        if (yaw < 90.0f) {
            yaw += 10.0f;
        }
    }
    TEST_ASSERT_TRUE(Motion_IsDone());
    writes_at_done = FakeMotorHw_GetWriteCount();
    for (i = 0; i < 5; i++) {
        drive_forward(40);
        push_yaw(90.0f);
        tick();
    }
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == writes_at_done);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_DONE);
    printf("PASS: test_arc_done_is_silent\n");
    return 0;
}

/* ARC 中 Motion_Stop：任意态 → Chassis_Stop（刹车）+ IDLE。 */
static int test_stop_during_arc_idles_and_brakes(void)
{
    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    drive_forward(40); push_yaw(10.0f); tick();
    Motion_Stop();
    TEST_ASSERT_TRUE(Motion_GetState() == MOTION_IDLE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    printf("PASS: test_stop_during_arc_idles_and_brakes\n");
    return 0;
}

/* ARC 里程计一次性消费：Update 快于底盘采样时 total 不变 → 位姿不二次前进（无双计数）。 */
static int test_arc_odometry_consumed_once(void)
{
    Motion_Telemetry_T t1;
    Motion_Telemetry_T t2;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    drive_forward(100); push_yaw(10.0f); tick();   /* 底盘采样→total=100 */
    push_yaw(10.0f); tick();                        /* 消费 d=100 → 位姿前进 */
    Motion_GetTelemetry(&t1);

    Motion_Update();   /* 不推进时钟/不改 raw → total 不变 → 不二次前进 */
    Motion_GetTelemetry(&t2);
    TEST_ASSERT_NEAR(t2.x_mm, t1.x_mm, 1e-6f);
    printf("PASS: test_arc_odometry_consumed_once\n");
    return 0;
}

/* ARC 遥测：state=ARC、target=arc_deg、progress 起始≈0。 */
static int test_arc_telemetry_reflects_state(void)
{
    Motion_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Motion_StartArc(200.0f, 90.0f));
    Motion_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.state == MOTION_ARC);
    TEST_ASSERT_NEAR(t.target, 90.0f, 1e-6f);
    TEST_ASSERT_NEAR(t.progress, 0.0f, 1e-6f);
    printf("PASS: test_arc_telemetry_reflects_state\n");
    return 0;
}

/* GetTelemetry(NULL) 安全。 */
static int test_telemetry_null_safe(void)
{
    setup();
    Motion_GetTelemetry(NULL);
    TEST_ASSERT_TRUE(true);
    printf("PASS: test_telemetry_null_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_is_silent_and_idle();
    failures += test_start_straight_rejects_nonpositive();
    failures += test_straight_advances_pose_forward();
    failures += test_straight_completes_and_brakes();
    failures += test_done_is_silent_brake_persists();
    failures += test_idle_update_is_silent();
    failures += test_heading_hold_corrects_ccw_drift();
    failures += test_heading_hold_corrects_cw_drift();
    failures += test_invalid_imu_no_steer_but_measures();
    failures += test_start_turn_rejects_zero();
    failures += test_turn_ccw_drives_left_neg_right_pos();
    failures += test_turn_cw_reverses_sign();
    failures += test_turn_completes_at_tolerance();
    failures += test_odometry_consumed_once();
    failures += test_stop_from_any_state_idles_and_brakes();
    failures += test_imu_frame_parsed_into_pose_heading();
    failures += test_telemetry_reflects_state();
    failures += test_start_profiled_rejects_nonpositive();
    failures += test_profiled_starts_at_start_speed();
    failures += test_profiled_reaches_cruise_midway();
    failures += test_profiled_decelerates_near_target();
    failures += test_profiled_completes_and_brakes();
    failures += test_profiled_heading_hold_corrects_ccw();
    failures += test_profile_params_set_get_roundtrip();
    failures += test_profile_params_apply_to_run();
    failures += test_profiled_watchdog_stops_runaway();
    failures += test_profiled_watchdog_disabled_by_default();
    failures += test_get_profile_params_null_safe();
    failures += test_start_arc_rejects_invalid();
    failures += test_arc_feedforward_ratio_ccw();
    failures += test_arc_feedforward_ratio_cw();
    failures += test_arc_correction_underturn_ccw();
    failures += test_arc_correction_underturn_cw();
    failures += test_arc_completes_by_heading_and_brakes();
    failures += test_arc_done_is_silent();
    failures += test_stop_during_arc_idles_and_brakes();
    failures += test_arc_odometry_consumed_once();
    failures += test_arc_telemetry_reflects_state();
    failures += test_telemetry_null_safe();

    if (failures != 0) {
        printf("%d motion test(s) failed.\n", failures);
        return 1;
    }
    printf("All motion tests passed.\n");
    return 0;
}
