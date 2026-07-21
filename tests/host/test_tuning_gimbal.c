/**
 * @file    test_tuning_gimbal.c
 * @brief   Host tests for the gimbal position-loop tuning profile (W8, 契约 §30).
 *
 * 契约回顾：
 * - cmd 组（7）：XP/XD/YP/YD/DB/MS/GO；安全初值 增益 0、DB=10000、MS=1、GO=0；
 *   清洗唯一点在 TuningGimbal_Apply（负→0、MS<1→1）。
 * - tx 组（13，通道序）：err_x err_y delta_x delta_y cur_x cur_y state
 *   + XP XD YP YD DB MS（cmd 清洗后回显）。
 * - Enter：安全 cmd + 注册 + Gimbal_Stop + 立即应用（DB=10000 ⇒ 零出力：cur_pulse 恒 0）。
 * - GO：≥0.5 边沿一次性消费 → Gimbal_ReselectTopic（单次，不粘滞）。
 * - Exit：Gimbal_Stop + 清 VOFA profile，此后静默。
 *
 * 链接：真实 tuning/tuning_gimbal/tuning_chassis/gimbal/gimbal_stepbus/uart_vision/
 *       vision_aim/emm42/chassis 链 + uart_vofa + fake_uart_port/fake_clock/
 *       fake_motor_hw/fake_board_gpio（chassis 链仅因 tuning.c 分派被链入，不进入其 profile）。
 */
#include "app/service/tuning/tuning.h"

#include "app/service/chassis/chassis.h"
#include "app/service/gimbal/gimbal.h"
#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/motor/motor.h"
#include "driver/uart_vofa/uart_vofa.h"
#include "middleware/vision_aim/vision_aim.h"

#include <stdio.h>
#include <string.h>

/* fake 钩子（无头文件，按既有测试惯例 extern 声明）。 */
void FakeUartPort_ResetAll(void);
void FakeUartPort_PushVisionBytes(const uint8_t *data, uint32_t length);
void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length);
void FakeUartPort_CompleteVisionTx(void);
void FakeUartPort_CompleteVofaTx(void);
void FakeUartPort_CompleteStepmotorTx(void);
uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);
uint32_t FakeUartPort_CopyStepmotorTx(uint8_t *out, uint32_t capacity);
void FakeClock_Set(uint32_t now_ms);
void FakeClock_Advance(uint32_t delta_ms);
void FakeBoardGpio_SetRaw(int32_t left, int32_t right);
void UartVision_Init(void);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* JustFloat 帧：13 通道 ×4 字节 + 4 字节帧尾（契约 §30 tx 组） */
#define GT_FRAME_CHANNELS 13u
#define GT_FRAME_BYTES (GT_FRAME_CHANNELS * 4u + 4u)

/* 通道下标（契约 §30 通道序） */
#define GT_CH_ERR_X   0u
#define GT_CH_DELTA_X 2u
#define GT_CH_CUR_X   4u
#define GT_CH_STATE   6u
#define GT_CH_XP      7u
#define GT_CH_DB      11u
#define GT_CH_MS      12u

/* ---- 视觉帧组包（独立复算 CRC16-MODBUS，与 test_gimbal 同法） --------------- */

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

static void push_coord(float x, float y)
{
    uint8_t buf[32];
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

    crc = ref_crc16(body, (uint16_t)(1u + payload_len));
    buf[n++] = (uint8_t)(crc & 0xFFu);
    buf[n++] = (uint8_t)((crc >> 8) & 0xFFu);
    FakeUartPort_PushVisionBytes(buf, n);
}

static void push_ack(uint8_t main_task, uint8_t sub_task)
{
    uint8_t frame[4] = {0xFFu, main_task, sub_task, 0xFEu};
    FakeUartPort_PushVisionBytes(frame, 4u);
}

static void push_cmd(const char *text)
{
    FakeUartPort_PushVofaBytes((const uint8_t *)text, (uint32_t)strlen(text));
}

/* 抓取最近一帧并解出 count 个 float；返回帧长（0 = 无帧）。 */
static uint32_t copy_frame_floats(float *out, uint32_t count)
{
    uint8_t raw[GT_FRAME_BYTES + 16u];
    uint32_t len = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    uint32_t i;

    if (len >= GT_FRAME_BYTES) {
        for (i = 0u; i < count; i++) {
            memcpy(&out[i], &raw[i * 4u], 4u);
        }
    }
    return len;
}

/* ---- 装配 ------------------------------------------------------------------ */

static Gimbal_Config_T g_cfg;

static void fresh_cfg(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.aim.center_px[VISION_AIM_AXIS_X] = 320.0f;
    g_cfg.aim.center_px[VISION_AIM_AXIS_Y] = 240.0f;
    g_cfg.aim.deadband_px[VISION_AIM_AXIS_X] = 10000.0f;  /* Enter 会以安全 cmd 覆写为同值 */
    g_cfg.aim.deadband_px[VISION_AIM_AXIS_Y] = 10000.0f;
    g_cfg.aim.max_step_pulse[VISION_AIM_AXIS_X] = 1;
    g_cfg.aim.max_step_pulse[VISION_AIM_AXIS_Y] = 1;
    g_cfg.aim.sign[VISION_AIM_AXIS_X] = 1;
    g_cfg.aim.sign[VISION_AIM_AXIS_Y] = 1;
    g_cfg.aim.travel_limit_pulse[VISION_AIM_AXIS_X] = 0;  /* 不限幅（调参链路本身不测轴程） */
    g_cfg.aim.travel_limit_pulse[VISION_AIM_AXIS_Y] = 0;
    g_cfg.step_speed_rpm = 30u;
    g_cfg.coord_timeout_ms = 100u;
    g_cfg.ack_timeout_ms = 200u;
}

/* 标准装配序：与 app_compose GimbalTune on_enter 一致（Init → EnterProfile → SelectTopic）。 */
static void setup(void)
{
    Clock_Init();
    FakeClock_Set(1000u);
    FakeBoardGpio_SetRaw(0, 0);
    Motor_Init();
    Encoder_Init();
    Chassis_Init();
    FakeUartPort_ResetAll();
    UartVision_Init();
    (void)vofa_init();
    fresh_cfg();
    Gimbal_Init(&g_cfg);
    Tuning_Init();
}

/* 一拍：10ms → Tuning_Update（vofa_run→Apply→RefreshTx + 恒泵 Gimbal_Update）→ 完成两路 TX。 */
static void pump_once(void)
{
    FakeClock_Advance(10u);
    Tuning_Update();
    FakeUartPort_CompleteVofaTx();
    FakeUartPort_CompleteStepmotorTx();
}

/* 进 profile 并走完握手+ARMING，留在 AIMING 且 seq 基线已建立。 */
static void enter_and_arm(void)
{
    int k;

    (void)Tuning_EnterProfile(TUNING_PROFILE_GIMBAL_AIM);
    (void)Gimbal_SelectTopic(1u, 2u);      /* 契约顺序：EnterProfile 之后（Enter 内 Stop 会杀握手） */
    FakeUartPort_CompleteVisionTx();
    push_ack(1u, 2u);
    pump_once();                           /* HANDSHAKING → ARMING */
    for (k = 0; k < 6; k++) {
        pump_once();                       /* 逐拍一帧 enable/preset/clear（六帧） */
    }
    pump_once();                           /* AIMING 建立 seq 基线 */
}

/* ---- 测试 ------------------------------------------------------------------ */

/* Enter 零出力：大误差坐标连拍，cur_pulse 恒 0（DB=10000）；帧=13 通道且 DB 回显 10000。 */
static int test_enter_safe_zero_output_and_frame(void)
{
    Gimbal_Telemetry_T t;
    float ch[GT_FRAME_CHANNELS];
    int k;

    setup();
    TEST_ASSERT(Tuning_EnterProfile(TUNING_PROFILE_GIMBAL_AIM) == true);
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);   /* Enter 内确定性停 */

    (void)Gimbal_SelectTopic(1u, 2u);
    FakeUartPort_CompleteVisionTx();
    push_ack(1u, 2u);
    pump_once();
    for (k = 0; k < 7; k++) {
        pump_once();
    }
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_AIMING);

    for (k = 0; k < 3; k++) {
        push_coord(620.0f, 440.0f);        /* err=(300,200)，全在 DB=10000 死区内 */
        pump_once();
    }
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 0);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_Y] == 0);

    TEST_ASSERT(copy_frame_floats(ch, GT_FRAME_CHANNELS) == GT_FRAME_BYTES);
    TEST_ASSERT((ch[GT_CH_DB] > 9999.0f) && (ch[GT_CH_DB] < 10001.0f));
    TEST_ASSERT((ch[GT_CH_STATE] > 2.9f) && (ch[GT_CH_STATE] < 3.1f));   /* AIMING=3 */
    return 0;
}

/* cmd 应用：XP/DB/MS 下发后，误差坐标按新增益出增量（cmd→SetAimTuning→VisionAim 唯一链）。 */
static int test_cmd_apply_moves_axis(void)
{
    Gimbal_Telemetry_T t;

    setup();
    enter_and_arm();

    push_cmd("XP=0.2\nDB=6\nMS=48\n");
    push_coord(420.0f, 240.0f);            /* err_x=100 → 0.2*100=20 */
    pump_once();                           /* 同拍：先 Apply 再泵 Gimbal */
    Gimbal_GetTelemetry(&t);
    TEST_ASSERT(t.last_delta_pulse[VISION_AIM_AXIS_X] == 20);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_X] == 20);
    TEST_ASSERT(t.cur_pulse[VISION_AIM_AXIS_Y] == 0);      /* YP 仍 0 且 err_y=0 */
    return 0;
}

/* cmd 清洗：负增益/负死区→0、MS<1→1；回显显示清洗后值（清洗唯一点在 Apply）。 */
static int test_cmd_sanitize_clamps(void)
{
    float ch[GT_FRAME_CHANNELS];

    setup();
    enter_and_arm();

    push_cmd("XP=-5\nDB=-3\nMS=0\n");
    pump_once();                           /* 解析+应用+RefreshTx */
    pump_once();                           /* 下一拍发出携带清洗回显的帧 */
    TEST_ASSERT(copy_frame_floats(ch, GT_FRAME_CHANNELS) == GT_FRAME_BYTES);
    TEST_ASSERT((ch[GT_CH_XP] > -0.001f) && (ch[GT_CH_XP] < 0.001f));   /* -5 → 0 */
    TEST_ASSERT((ch[GT_CH_DB] > -0.001f) && (ch[GT_CH_DB] < 0.001f));   /* -3 → 0 */
    TEST_ASSERT((ch[GT_CH_MS] > 0.999f) && (ch[GT_CH_MS] < 1.001f));    /* 0 → 1 */

    push_cmd("MS=3000000000\n");           /* 上界（修订 2）：越 int32 域的超大值 → 10000 */
    pump_once();
    pump_once();
    TEST_ASSERT(copy_frame_floats(ch, GT_FRAME_CHANNELS) == GT_FRAME_BYTES);
    TEST_ASSERT((ch[GT_CH_MS] > 9999.0f) && (ch[GT_CH_MS] < 10001.0f));
    return 0;
}

/* GO 单次消费：STOPPED 后 GO=1 → 重握手；ack 到达可推进到 ARMING 且不被残留 GO 拉回。 */
static int test_go_single_consume_rearm(void)
{
    setup();
    enter_and_arm();

    Gimbal_Stop();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);

    push_cmd("GO=1\n");
    pump_once();                           /* Apply 消费 GO → ReselectTopic(1,2) */
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_HANDSHAKING);
    FakeUartPort_CompleteVisionTx();

    push_ack(1u, 2u);
    pump_once();                           /* ack 匹配 → ARMING；若 GO 粘滞会被拉回 HANDSHAKING */
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_ARMING);
    pump_once();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_ARMING);
    return 0;
}

/* Exit：确定性停 + 清表；此后 Update 完全静默（无 VOFA 帧、无步进 TX）。 */
static int test_exit_stops_and_silent(void)
{
    uint8_t raw[64];
    int k;

    setup();
    enter_and_arm();

    Tuning_ExitProfile();
    TEST_ASSERT(Gimbal_GetState() == GIMBAL_STATE_STOPPED);
    TEST_ASSERT(Tuning_GetActiveProfile() == TUNING_PROFILE_NONE);

    FakeUartPort_ResetAll();               /* 清 TX 抓取面，隔离退出后的观察 */
    UartVision_Init();
    for (k = 0; k < 3; k++) {
        FakeClock_Advance(10u);
        Tuning_Update();
    }
    TEST_ASSERT(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u);
    TEST_ASSERT(FakeUartPort_CopyStepmotorTx(raw, sizeof(raw)) == 0u);
    return 0;
}

/* 重进重置安全：调过增益后 Exit+重 Enter，cmd 组回安全值（增益 0/DB=10000/MS=1）。 */
static int test_reenter_resets_safe(void)
{
    float ch[GT_FRAME_CHANNELS];

    setup();
    enter_and_arm();
    push_cmd("XP=0.5\nDB=6\nMS=48\n");
    pump_once();

    Tuning_ExitProfile();
    TEST_ASSERT(Tuning_EnterProfile(TUNING_PROFILE_GIMBAL_AIM) == true);
    pump_once();
    pump_once();
    TEST_ASSERT(copy_frame_floats(ch, GT_FRAME_CHANNELS) == GT_FRAME_BYTES);
    TEST_ASSERT((ch[GT_CH_XP] > -0.001f) && (ch[GT_CH_XP] < 0.001f));
    TEST_ASSERT((ch[GT_CH_DB] > 9999.0f) && (ch[GT_CH_DB] < 10001.0f));
    TEST_ASSERT((ch[GT_CH_MS] > 0.999f) && (ch[GT_CH_MS] < 1.001f));
    return 0;
}

#define RUN(fn) do { if ((fn)() != 0) { fails++; } else { printf("PASS: %s\n", #fn); } } while (0)

int main(void)
{
    int fails = 0;

    RUN(test_enter_safe_zero_output_and_frame);
    RUN(test_cmd_apply_moves_axis);
    RUN(test_cmd_sanitize_clamps);
    RUN(test_go_single_consume_rearm);
    RUN(test_exit_stops_and_silent);
    RUN(test_reenter_resets_safe);

    if (fails == 0) {
        printf("All tuning_gimbal tests passed.\n");
    }
    return (fails == 0) ? 0 : 1;
}
