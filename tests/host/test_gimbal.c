/**
 * @file    test_gimbal.c
 * @brief   Host tests for gimbal 云台视觉瞄准服务 (S05c, 契约 §21.3).
 *
 * 覆盖：Init 静默；选题握手（发 0xFF 帧 / TX 忙 / 确认匹配→ARMING / 号不匹配保持 / ack 超时→STOPPED）；
 * ARMING 逐拍 enable×2+setzero×2→AIMING；AIMING 像素闭环下发相对移动 + cur_pulse 仅成功下发才累加；
 * 总线忙不累加；死区不下发；坐标停顿保持后超时→STOPPED；Gimbal_Stop 确定性；轴程限幅经 vision_aim 生效。
 *
 * 链接：真实 gimbal/gimbal_stepbus/uart_vision/vision_aim/emm42/stepmotor_uart/board_uart×4
 *       + fake_uart_port（视觉 RX 注入 + 步进 TX 抓取/完成）+ fake_clock。
 */
#include "app/service/gimbal/gimbal.h"

#include "driver/step_motor/emm42.h"
#include "driver/uart_vision/uart_vision.h"
#include "middleware/vision_aim/vision_aim.h"

#include <stdio.h>
#include <string.h>

/* fake 钩子（无头文件，按既有测试惯例 extern 声明）。 */
void FakeUartPort_ResetAll(void);
void FakeUartPort_PushVisionBytes(const uint8_t *data, uint32_t length);
uint32_t FakeUartPort_CopyVisionTx(uint8_t *out, uint32_t capacity);
void FakeUartPort_CompleteStepmotorTx(void);
uint32_t FakeUartPort_CopyStepmotorTx(uint8_t *out, uint32_t capacity);
void FakeClock_Set(uint32_t now_ms);
void FakeClock_Advance(uint32_t delta_ms);
void UartVision_Init(void);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* ---- 视觉帧组包（独立复算 CRC16-MODBUS，与 test_uart_vision 同法） ---------- */

static uint16_t ref_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t b;
    for (i = 0u; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (b = 0u; b < 8u; b++) {
            crc = ((crc & 1u) != 0u) ? (uint16_t)((crc >> 1) ^ 0xA001u)
                                     : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* 0xAA 0x55 | len | [cmd(0x01) | x float | y float] | CRC16 LE，返回整帧长。 */
static uint32_t build_coord_frame(uint8_t *buf, float x, float y)
{
    uint8_t body[16];
    uint16_t crc;
    uint32_t n = 0u;
    uint8_t payload_len = 9u; /* cmd + 8B */

    buf[n++] = 0xAAu;
    buf[n++] = 0x55u;
    buf[n++] = payload_len;

    body[0] = payload_len;
    body[1] = 0x01u; /* cmd = coord */
    memcpy(&body[2], &x, sizeof(float));
    memcpy(&body[6], &y, sizeof(float));

    buf[n++] = 0x01u;
    memcpy(&buf[n], &x, sizeof(float)); n += 4u;
    memcpy(&buf[n], &y, sizeof(float)); n += 4u;

    crc = ref_crc16(body, (uint16_t)(1u + payload_len)); /* len 字节 + payload */
    buf[n++] = (uint8_t)(crc & 0xFFu);
    buf[n++] = (uint8_t)((crc >> 8) & 0xFFu);
    return n;
}

static void push_coord(float x, float y)
{
    uint8_t frame[32];
    uint32_t len = build_coord_frame(frame, x, y);
    FakeUartPort_PushVisionBytes(frame, len);
}

static void push_ack(uint8_t main_task, uint8_t sub_task)
{
    uint8_t frame[4] = {0xFFu, main_task, sub_task, 0xFEu};
    FakeUartPort_PushVisionBytes(frame, 4u);
}

/* ---- 配置 ------------------------------------------------------------------ */

static Gimbal_Config_T g_cfg;

static void fresh_cfg(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.aim.center_px[VISION_AIM_AXIS_X] = 320.0f;
    g_cfg.aim.center_px[VISION_AIM_AXIS_Y] = 240.0f;
    g_cfg.aim.deadband_px[VISION_AIM_AXIS_X] = 6.0f;
    g_cfg.aim.deadband_px[VISION_AIM_AXIS_Y] = 6.0f;
    g_cfg.aim.kp[VISION_AIM_AXIS_X] = 0.1f;
    g_cfg.aim.kp[VISION_AIM_AXIS_Y] = 0.1f;
    g_cfg.aim.max_step_pulse[VISION_AIM_AXIS_X] = 48;
    g_cfg.aim.max_step_pulse[VISION_AIM_AXIS_Y] = 48;
    g_cfg.aim.sign[VISION_AIM_AXIS_X] = 1;
    g_cfg.aim.sign[VISION_AIM_AXIS_Y] = 1;
    g_cfg.aim.travel_limit_pulse[VISION_AIM_AXIS_X] = 800;
    g_cfg.aim.travel_limit_pulse[VISION_AIM_AXIS_Y] = 400;
    g_cfg.step_speed_rpm = 30u;
    g_cfg.coord_timeout_ms = 100u;
    g_cfg.ack_timeout_ms = 200u;
}

static void base_init(void)
{
    fresh_cfg();
    FakeClock_Set(0u);
    FakeUartPort_ResetAll();
    UartVision_Init();
    Gimbal_Init(&g_cfg);
}

/* 走完握手 + ARMING，留在 AIMING 且已建立 seq 基线（g_cfg 由调用方先设好）。 */
static void arm_to_aiming(void)
{
    int k;

    FakeClock_Set(0u);
    FakeUartPort_ResetAll();
    UartVision_Init();
    Gimbal_Init(&g_cfg);

    Gimbal_SelectTopic(1u, 2u);
    push_ack(1u, 2u);
    FakeClock_Advance(10u);
    Gimbal_Update();               /* HANDSHAKING → ARMING */

    for (k = 0; k < 4; k++) {
        FakeClock_Advance(10u);
        Gimbal_Update();           /* 逐拍一帧 enable/setzero */
        FakeUartPort_CompleteStepmotorTx();
    }
    /* 现应 AIMING，has_coord_seq=false */
    FakeClock_Advance(10u);
    Gimbal_Update();               /* AIMING 建立 seq 基线 */
}

/* ---- 测试 ------------------------------------------------------------------ */

static int test_init_silent(void)
{
    uint8_t b[32];
    Gimbal_Telemetry_T t;
    base_init();

    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_IDLE);
    TEST_ASSERT(FakeUartPort_CopyVisionTx(b, sizeof(b)) == 0u);      /* 未发选题 */
    TEST_ASSERT(FakeUartPort_CopyStepmotorTx(b, sizeof(b)) == 0u);   /* 未发电机命令 */
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 0);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_Y] == 0);
    TEST_ASSERT(t.state == GIMBAL_STATE_IDLE);
    return 0;
}

static int test_select_topic_sends_frame(void)
{
    uint8_t b[32];
    uint32_t n;
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(3u, 4u) == true);
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_HANDSHAKING);
    n = FakeUartPort_CopyVisionTx(b, sizeof(b));
    TEST_ASSERT(n == 4u);
    TEST_ASSERT((b[0] == 0xFFu) && (b[1] == 3u) && (b[2] == 4u) && (b[3] == 0xFEu));
    return 0;
}

static int test_select_topic_tx_busy_returns_false(void)
{
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(1u, 2u) == true);   /* 视觉 TX 忙（未完成） */
    TEST_ASSERT(Gimbal_SelectTopic(3u, 4u) == false);  /* TX 忙 → 拒绝、保持原态 */
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_HANDSHAKING);
    return 0;
}

static int test_ack_match_enters_arming(void)
{
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(1u, 2u) == true);
    push_ack(1u, 2u);
    FakeClock_Advance(10u);
    Gimbal_Update();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_ARMING);
    return 0;
}

static int test_ack_mismatch_stays_handshaking(void)
{
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(1u, 2u) == true);
    push_ack(9u, 9u);   /* 回显号不匹配 */
    FakeClock_Advance(10u);
    Gimbal_Update();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_HANDSHAKING);
    return 0;
}

static int test_ack_timeout_stops(void)
{
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(1u, 2u) == true);  /* 不推确认帧 */
    FakeClock_Advance(201u);                          /* 超 ack_timeout_ms=200 */
    Gimbal_Update();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);
    return 0;
}

static int expect_stepmotor_tx(const uint8_t *expect, uint8_t expect_len)
{
    uint8_t got[32];
    uint32_t got_len = FakeUartPort_CopyStepmotorTx(got, sizeof(got));
    if (got_len != (uint32_t)expect_len) {
        printf("FAIL: stepmotor tx len %u != %u\n", got_len, (unsigned)expect_len);
        return 1;
    }
    if (memcmp(got, expect, expect_len) != 0) {
        printf("FAIL: stepmotor tx bytes mismatch\n");
        return 1;
    }
    return 0;
}

static int test_arming_sequence_to_aiming(void)
{
    uint8_t exp[32];
    uint8_t el = 0u;
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(1u, 2u) == true);
    push_ack(1u, 2u);
    FakeClock_Advance(10u);
    Gimbal_Update();   /* → ARMING */

    /* 帧1 enable X */
    FakeClock_Advance(10u); Gimbal_Update();
    Emm42_BuildEnableFrame((uint8_t)EMM42_AXIS_X, EMM42_ENABLE_ON, exp, &el);
    if (expect_stepmotor_tx(exp, el) != 0) { return 1; }
    FakeUartPort_CompleteStepmotorTx();

    /* 帧2 enable Y */
    FakeClock_Advance(10u); Gimbal_Update();
    Emm42_BuildEnableFrame((uint8_t)EMM42_AXIS_Y, EMM42_ENABLE_ON, exp, &el);
    if (expect_stepmotor_tx(exp, el) != 0) { return 1; }
    FakeUartPort_CompleteStepmotorTx();

    /* 帧3 setzero X */
    FakeClock_Advance(10u); Gimbal_Update();
    Emm42_BuildSetZeroFrame((uint8_t)EMM42_AXIS_X, exp, &el);
    if (expect_stepmotor_tx(exp, el) != 0) { return 1; }
    FakeUartPort_CompleteStepmotorTx();

    /* 帧4 setzero Y → AIMING */
    FakeClock_Advance(10u); Gimbal_Update();
    Emm42_BuildSetZeroFrame((uint8_t)EMM42_AXIS_Y, exp, &el);
    if (expect_stepmotor_tx(exp, el) != 0) { return 1; }
    FakeUartPort_CompleteStepmotorTx();

    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_AIMING);
    return 0;
}

static int test_arming_timeout_stops(void)
{
    base_init();

    TEST_ASSERT(Gimbal_SelectTopic(1u, 2u) == true);
    push_ack(1u, 2u);
    FakeClock_Advance(10u);
    Gimbal_Update();                 /* → ARMING（s_arm_start = now） */
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_ARMING);

    /* 总线永不空闲（不完成步进 TX）：发出首帧后卡住 */
    FakeClock_Advance(10u);
    Gimbal_Update();                 /* 发 enable X，总线忙 */
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_ARMING);

    /* 超 ack_timeout_ms=200 → 安全停（不永久滞留已使能态） */
    FakeClock_Advance(201u);
    Gimbal_Update();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);
    return 0;
}

static int test_aiming_dispatch_accumulate(void)
{
    uint8_t exp[32];
    uint8_t el = 0u;
    Gimbal_Telemetry_T t;

    fresh_cfg();
    arm_to_aiming();

    push_coord(420.0f, 240.0f);   /* err_x=100 → delta 10 CW；err_y=0 → 死区 */
    FakeClock_Advance(10u);
    Gimbal_Update();

    Emm42_BuildPositionFrame((uint8_t)EMM42_AXIS_X, EMM42_DIR_CW, 30u,
                             EMM42_POSITION_ACCEL_FIXED, 10u,
                             EMM42_POSITION_MODE_RELATIVE, exp, &el);
    if (expect_stepmotor_tx(exp, el) != 0) { return 1; }

    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_Y] == 0);
    TEST_ASSERT(t.axis_active[VISION_AIM_AXIS_X] == true);
    TEST_ASSERT(t.axis_active[VISION_AIM_AXIS_Y] == false);
    TEST_ASSERT(t.state == GIMBAL_STATE_AIMING);
    return 0;
}

static int test_aiming_bus_busy_no_accumulate(void)
{
    Gimbal_Telemetry_T t;

    fresh_cfg();
    arm_to_aiming();

    push_coord(420.0f, 240.0f);   /* 首帧：dispatch X=10，总线忙（不完成 TX） */
    FakeClock_Advance(10u);
    Gimbal_Update();
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);

    push_coord(520.0f, 240.0f);   /* 新坐标：想再走 20，但总线仍忙 → 拒发 → 不累加 */
    FakeClock_Advance(10u);
    Gimbal_Update();
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);   /* 仍 10，未变 30 */
    return 0;
}

static int test_deadband_no_dispatch(void)
{
    Gimbal_Telemetry_T t;

    fresh_cfg();
    arm_to_aiming();

    push_coord(323.0f, 242.0f);   /* err_x=3, err_y=2，均 ≤ 死区 6 */
    FakeClock_Advance(10u);
    Gimbal_Update();

    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 0);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_Y] == 0);
    TEST_ASSERT(t.axis_active[VISION_AIM_AXIS_X] == false);
    TEST_ASSERT(t.axis_active[VISION_AIM_AXIS_Y] == false);
    return 0;
}

static int test_coord_stall_then_timeout(void)
{
    fresh_cfg();
    arm_to_aiming();

    push_coord(420.0f, 240.0f);
    FakeClock_Advance(10u);
    Gimbal_Update();                 /* fresh：last_fresh 更新 */
    FakeUartPort_CompleteStepmotorTx();

    FakeClock_Advance(50u);          /* 停顿 50 < 100 → 保持 AIMING */
    Gimbal_Update();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_AIMING);

    FakeClock_Advance(60u);          /* 累计停顿 110 ≥ 100 → STOPPED */
    Gimbal_Update();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);
    return 0;
}

static int test_stop_deterministic(void)
{
    Gimbal_Telemetry_T t;

    fresh_cfg();
    arm_to_aiming();

    push_coord(420.0f, 240.0f);
    FakeClock_Advance(10u);
    Gimbal_Update();                 /* cur_pulse[X]=10, AIMING */

    Gimbal_Stop();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);   /* 位置保留 */

    /* STOPPED 后不再下发 */
    FakeUartPort_CompleteStepmotorTx();
    push_coord(600.0f, 240.0f);
    FakeClock_Advance(10u);
    Gimbal_Update();
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);
    return 0;
}

static int test_travel_limit_clamp(void)
{
    Gimbal_Telemetry_T t;

    fresh_cfg();
    g_cfg.aim.travel_limit_pulse[VISION_AIM_AXIS_X] = 5;   /* 一拍即撞轴程 */
    arm_to_aiming();

    push_coord(420.0f, 240.0f);   /* 原始 delta 10，被 vision_aim 截到 5（cur 0 → 不越 +5） */
    FakeClock_Advance(10u);
    Gimbal_Update();

    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 5);
    return 0;
}

/* §21.4 PD：进 AIMING 首帧播种 prev（de=0）→ 纯 P，无首拍 D 冲击。 */
static int test_aiming_pd_first_frame_seeds_no_kick(void)
{
    Gimbal_Telemetry_T t;

    fresh_cfg();
    g_cfg.aim.kd[VISION_AIM_AXIS_X] = 0.2f;   /* 开 X 轴微分（kp=0.1） */
    arm_to_aiming();

    /* err_x=100：未播种(prev=0)时 D 会顶到 0.1*100+0.2*100=30；播种令 de=0 → 纯 P=10。 */
    push_coord(420.0f, 240.0f);
    FakeClock_Advance(10u);
    Gimbal_Update();

    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);   /* 首拍纯 P，非 30 */
    return 0;
}

/* §21.4 PD：逐拍更新 prev → 误差扩大时 D 助推、缩小时 D 阻尼；cur_pulse 仅成功下发才累加。 */
static int test_aiming_pd_boost_and_damp_across_frames(void)
{
    Gimbal_Telemetry_T t;

    fresh_cfg();
    g_cfg.aim.kd[VISION_AIM_AXIS_X] = 0.2f;
    arm_to_aiming();

    /* 帧1 err=100：播种，纯 P=10 → cur=10 */
    push_coord(420.0f, 240.0f);
    FakeClock_Advance(10u); Gimbal_Update();
    FakeUartPort_CompleteStepmotorTx();
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 10);

    /* 帧2 err 增至 200：prev=100 → de=100 → raw=0.1*200+0.2*100=40 → cur=50（助推，纯 P 仅 20） */
    push_coord(520.0f, 240.0f);
    FakeClock_Advance(10u); Gimbal_Update();
    FakeUartPort_CompleteStepmotorTx();
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 50);

    /* 帧3 err 缩至 150：prev=200 → de=-50 → raw=0.1*150+0.2*(-50)=5 → cur=55（阻尼，纯 P 应为 15） */
    push_coord(470.0f, 240.0f);
    FakeClock_Advance(10u); Gimbal_Update();
    FakeUartPort_CompleteStepmotorTx();
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 55);
    return 0;
}

#define RUN(fn) do { if ((fn)() != 0) { fails++; } else { printf("PASS: %s\n", #fn); } } while (0)

int main(void)
{
    int fails = 0;

    RUN(test_init_silent);
    RUN(test_select_topic_sends_frame);
    RUN(test_select_topic_tx_busy_returns_false);
    RUN(test_ack_match_enters_arming);
    RUN(test_ack_mismatch_stays_handshaking);
    RUN(test_ack_timeout_stops);
    RUN(test_arming_sequence_to_aiming);
    RUN(test_arming_timeout_stops);
    RUN(test_aiming_dispatch_accumulate);
    RUN(test_aiming_bus_busy_no_accumulate);
    RUN(test_deadband_no_dispatch);
    RUN(test_coord_stall_then_timeout);
    RUN(test_stop_deterministic);
    RUN(test_travel_limit_clamp);
    RUN(test_aiming_pd_first_frame_seeds_no_kick);
    RUN(test_aiming_pd_boost_and_damp_across_frames);

    if (fails == 0) {
        printf("All gimbal tests passed.\n");
    }
    return (fails == 0) ? 0 : 1;
}
