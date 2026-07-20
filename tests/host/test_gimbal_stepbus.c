/**
 * @file    test_gimbal_stepbus.c
 * @brief   Host tests for gimbal_stepbus 双轴绝对协同派发 (T-GQ2, 契约 plan_gimbal_qpos_codrive).
 *
 * 覆盖：双轴绝对目标一帧封装（0xAA 裹 FC_Y∥FC_X，Y=addr1 在前）+ F1 预设 + 0x0A 清零 + enable、
 * TX 忙拒发 / 完成重开、Service drain RX。交叉校验：用真实 Emm42_Build*Frame 组「期望帧」与被测
 * 捕获的 TX 逐字节比对——既证 gimbal_stepbus 的轴映射/拼装/封装，又不重复硬编码字节布局。
 */
#include "app/service/gimbal/gimbal_stepbus.h"

#include "driver/board_uart/stepmotor_uart.h"
#include "driver/step_motor/emm42.h"

#include <stdio.h>
#include <string.h>

/* fake_uart_port 抓取/完成/注入钩子（无头文件，按既有测试惯例 extern 声明）。 */
void FakeUartPort_ResetAll(void);
void FakeUartPort_CompleteStepmotorTx(void);
uint32_t FakeUartPort_CopyStepmotorTx(uint8_t *out, uint32_t capacity);
void FakeUartPort_PushStepmotorBytes(const uint8_t *data, uint32_t length);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static void setup(void)
{
    FakeUartPort_ResetAll();   /* 复位 stepmotor_uart 字节层（清 tx_busy/last_tx） */
    GimbalStepbus_Init();
}

/* 捕获最近一帧 TX 并与期望帧比对。 */
static int expect_last_tx(const uint8_t *expect, uint8_t expect_len)
{
    uint8_t got[32];
    uint32_t got_len = FakeUartPort_CopyStepmotorTx(got, sizeof(got));

    if (got_len != (uint32_t)expect_len) {
        printf("FAIL: tx len %u != %u\n", got_len, (unsigned)expect_len);
        return 1;
    }
    if (memcmp(got, expect, expect_len) != 0) {
        printf("FAIL: tx bytes mismatch\n");
        return 1;
    }
    return 0;
}

/* 用真实 emm42 builder 组「双轴绝对协同」期望帧：0xAA 裹 FC_Y(y)∥FC_X(x)，Y(addr1) 在前。 */
static uint8_t build_expected_dual(int32_t x_pulse, int32_t y_pulse, uint8_t *exp)
{
    uint8_t fy[8];
    uint8_t fx[8];
    uint8_t ly = 0u;
    uint8_t lx = 0u;
    uint8_t subs[16];
    uint8_t exp_len = 0u;

    Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_Y, y_pulse, fy, &ly);
    Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_X, x_pulse, fx, &lx);
    memcpy(subs, fy, ly);
    memcpy(subs + ly, fx, lx);
    Emm42_BuildMultiCmdFrame(subs, (uint8_t)(ly + lx), exp, &exp_len);
    return exp_len;
}

/* ---- 双轴绝对协同帧 -------------------------------------------------------- */

static int test_dual_absolute_positive(void)
{
    uint8_t exp[32];
    uint8_t el;
    setup();

    el = build_expected_dual(100, 50, exp);   /* X=100(id2), Y=50(id1) */
    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(100, 50) == true);
    return expect_last_tx(exp, el);
}

static int test_dual_absolute_negative_targets(void)
{
    uint8_t exp[32];
    uint8_t el;
    setup();

    /* 负绝对目标：符号由 int32 承载（无 dir 字节、无幅值拆分）。 */
    el = build_expected_dual(-50, -2048, exp);
    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(-50, -2048) == true);
    return expect_last_tx(exp, el);
}

static int test_dual_absolute_zero_still_sends(void)
{
    uint8_t exp[32];
    uint8_t el;
    setup();

    /* 绝对方案：目标未变（0,0）仍恒发一帧（幂等，保帧长与心跳节奏）。 */
    el = build_expected_dual(0, 0, exp);
    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(0, 0) == true);
    return expect_last_tx(exp, el);
}

/* ---- F1 预设 / 0x0A 清零 / enable ------------------------------------------ */

static int test_preset_frame(void)
{
    uint8_t exp[32];
    uint8_t el = 0u;
    setup();

    /* 预设内部固定 mode=ABSOLUTE、accel=0；仅速度可配，交 emm42 唯一限幅。 */
    Emm42_BuildQPosPresetFrame((uint8_t)EMM42_AXIS_X, 30u, EMM42_POSITION_ACCEL_FIXED,
                               EMM42_POSITION_MODE_ABSOLUTE, exp, &el);
    TEST_ASSERT(GimbalStepbus_TrySendPreset(GIMBAL_STEPBUS_AXIS_X, 30u) == true);
    return expect_last_tx(exp, el);
}

static int test_preset_speed_passthrough_to_emm42_clamp(void)
{
    uint8_t exp[32];
    uint8_t el = 0u;
    setup();

    /* speed=250 原样透传 emm42（唯一限幅所有者夹到 100 ×10）；两侧同经 emm42 → 相等。 */
    Emm42_BuildQPosPresetFrame((uint8_t)EMM42_AXIS_Y, 250u, EMM42_POSITION_ACCEL_FIXED,
                               EMM42_POSITION_MODE_ABSOLUTE, exp, &el);
    TEST_ASSERT(GimbalStepbus_TrySendPreset(GIMBAL_STEPBUS_AXIS_Y, 250u) == true);
    return expect_last_tx(exp, el);
}

static int test_clearzero_frame(void)
{
    uint8_t exp[32];
    uint8_t el = 0u;
    setup();

    /* 绝对坐标零点用 0x0A（清当前位置），非 0x93 单圈回零。 */
    Emm42_BuildClearPositionFrame((uint8_t)EMM42_AXIS_Y, exp, &el);
    TEST_ASSERT(GimbalStepbus_TrySendClearZero(GIMBAL_STEPBUS_AXIS_Y) == true);
    return expect_last_tx(exp, el);
}

static int test_enable_frame(void)
{
    uint8_t exp[32];
    uint8_t el = 0u;
    setup();

    Emm42_BuildEnableFrame((uint8_t)EMM42_AXIS_X, EMM42_ENABLE_ON, exp, &el);
    TEST_ASSERT(GimbalStepbus_TrySendEnable(GIMBAL_STEPBUS_AXIS_X, true) == true);
    return expect_last_tx(exp, el);
}

/* ---- TX 忙阻塞 / 完成重开 -------------------------------------------------- */

static int test_tx_busy_blocks_second(void)
{
    uint8_t exp[32];
    uint8_t el;
    setup();

    el = build_expected_dual(7, 9, exp);
    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(7, 9) == true);
    TEST_ASSERT(GimbalStepbus_IsIdle() == false);
    /* 总线忙：第二帧被拒且不覆盖首帧 */
    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(11, 13) == false);
    return expect_last_tx(exp, el);
}

static int test_complete_tx_reenables(void)
{
    setup();

    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(7, 9) == true);
    TEST_ASSERT(GimbalStepbus_IsIdle() == false);
    FakeUartPort_CompleteStepmotorTx();
    TEST_ASSERT(GimbalStepbus_IsIdle() == true);
    TEST_ASSERT(GimbalStepbus_TrySendDualAbsolute(11, 13) == true);
    return 0;
}

/* ---- Service drain RX ------------------------------------------------------ */

static int test_service_drains_rx(void)
{
    uint8_t junk[5] = {0x01u, 0x02u, 0x6Bu, 0xAAu, 0x55u};
    uint8_t buf[32];
    setup();

    FakeUartPort_PushStepmotorBytes(junk, sizeof(junk));
    GimbalStepbus_Service();
    /* drain 后字节层 FIFO 应为空 */
    TEST_ASSERT(StepmotorUart_Read(buf, sizeof(buf)) == 0u);
    return 0;
}

#define RUN(fn) do { if ((fn)() != 0) { fails++; } else { printf("PASS: %s\n", #fn); } } while (0)

int main(void)
{
    int fails = 0;

    RUN(test_dual_absolute_positive);
    RUN(test_dual_absolute_negative_targets);
    RUN(test_dual_absolute_zero_still_sends);
    RUN(test_preset_frame);
    RUN(test_preset_speed_passthrough_to_emm42_clamp);
    RUN(test_clearzero_frame);
    RUN(test_enable_frame);
    RUN(test_tx_busy_blocks_second);
    RUN(test_complete_tx_reenables);
    RUN(test_service_drains_rx);

    if (fails == 0) {
        printf("All gimbal_stepbus tests passed.\n");
    }
    return (fails == 0) ? 0 : 1;
}
