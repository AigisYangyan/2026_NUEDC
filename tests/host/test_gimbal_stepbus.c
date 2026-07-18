/**
 * @file    test_gimbal_stepbus.c
 * @brief   Host tests for gimbal_stepbus (S05c, 契约 §21.3).
 *
 * 覆盖：脉冲→dir+magnitude 拆分 + emm42 相对位置组包 + 轴 id 映射 + speed 透传 emm42 限幅、
 * enable/set-zero 帧、pulses==0 不发、TX 忙阻塞、完成后重开、Service drain RX。
 * 交叉校验方式：用真实 Emm42_Build*Frame 组「期望帧」与被测捕获的 TX 逐字节比对——
 * 既证 gimbal_stepbus 的轴/方向/幅值映射，又不重复硬编码字节布局。
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

/* ---- 相对移动帧：轴 id / 方向 / 幅值 / 速度 --------------------------------- */

static int test_relative_x_positive(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    /* 期望：X 轴(id=2)、正脉冲→CW、magnitude=12、speed=30、相对模式 */
    Emm42_BuildPositionFrame((uint8_t)EMM42_AXIS_X, EMM42_DIR_CW, 30u,
                             EMM42_POSITION_ACCEL_FIXED, 12u,
                             EMM42_POSITION_MODE_RELATIVE, exp, &exp_len);

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_X, 12, 30u) == true);
    return expect_last_tx(exp, exp_len);
}

static int test_relative_x_negative_dir(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    /* 负脉冲 → CCW、magnitude=|−12|=12 */
    Emm42_BuildPositionFrame((uint8_t)EMM42_AXIS_X, EMM42_DIR_CCW, 30u,
                             EMM42_POSITION_ACCEL_FIXED, 12u,
                             EMM42_POSITION_MODE_RELATIVE, exp, &exp_len);

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_X, -12, 30u) == true);
    return expect_last_tx(exp, exp_len);
}

static int test_relative_y_axis_id(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    Emm42_BuildPositionFrame((uint8_t)EMM42_AXIS_Y, EMM42_DIR_CW, 30u,
                             EMM42_POSITION_ACCEL_FIXED, 5u,
                             EMM42_POSITION_MODE_RELATIVE, exp, &exp_len);

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_Y, 5, 30u) == true);
    return expect_last_tx(exp, exp_len);
}

static int test_relative_speed_clamped_by_emm42(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    /* speed=250 传下去，emm42 是唯一限幅所有者（夹到 100 再 ×10）；期望帧同样用 250 组，
     * 两侧都经 emm42 夹紧 → 相等，证 gimbal_stepbus 未自夹、原样透传。 */
    Emm42_BuildPositionFrame((uint8_t)EMM42_AXIS_X, EMM42_DIR_CW, 250u,
                             EMM42_POSITION_ACCEL_FIXED, 3u,
                             EMM42_POSITION_MODE_RELATIVE, exp, &exp_len);

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_X, 3, 250u) == true);
    return expect_last_tx(exp, exp_len);
}

static int test_relative_zero_pulses_no_send(void)
{
    uint8_t got[32];
    setup();

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_X, 0, 30u) == false);
    /* 未发任何帧：last_tx 长度 0 */
    TEST_ASSERT(FakeUartPort_CopyStepmotorTx(got, sizeof(got)) == 0u);
    return 0;
}

/* ---- TX 忙阻塞 / 完成重开 -------------------------------------------------- */

static int test_tx_busy_blocks_second(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    Emm42_BuildPositionFrame((uint8_t)EMM42_AXIS_X, EMM42_DIR_CW, 30u,
                             EMM42_POSITION_ACCEL_FIXED, 7u,
                             EMM42_POSITION_MODE_RELATIVE, exp, &exp_len);

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_X, 7, 30u) == true);
    TEST_ASSERT(GimbalStepbus_IsIdle() == false);
    /* 总线忙：第二帧被拒且不覆盖首帧 */
    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_Y, 9, 30u) == false);
    return expect_last_tx(exp, exp_len);
}

static int test_complete_tx_reenables(void)
{
    setup();

    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_X, 7, 30u) == true);
    TEST_ASSERT(GimbalStepbus_IsIdle() == false);
    FakeUartPort_CompleteStepmotorTx();
    TEST_ASSERT(GimbalStepbus_IsIdle() == true);
    TEST_ASSERT(GimbalStepbus_TrySendRelative(GIMBAL_STEPBUS_AXIS_Y, 9, 30u) == true);
    return 0;
}

/* ---- enable / set-zero 帧 -------------------------------------------------- */

static int test_enable_frame(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    Emm42_BuildEnableFrame((uint8_t)EMM42_AXIS_X, EMM42_ENABLE_ON, exp, &exp_len);
    TEST_ASSERT(GimbalStepbus_TrySendEnable(GIMBAL_STEPBUS_AXIS_X, true) == true);
    return expect_last_tx(exp, exp_len);
}

static int test_setzero_frame(void)
{
    uint8_t exp[32];
    uint8_t exp_len = 0u;
    setup();

    Emm42_BuildSetZeroFrame((uint8_t)EMM42_AXIS_Y, exp, &exp_len);
    TEST_ASSERT(GimbalStepbus_TrySendSetZero(GIMBAL_STEPBUS_AXIS_Y) == true);
    return expect_last_tx(exp, exp_len);
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

    RUN(test_relative_x_positive);
    RUN(test_relative_x_negative_dir);
    RUN(test_relative_y_axis_id);
    RUN(test_relative_speed_clamped_by_emm42);
    RUN(test_relative_zero_pulses_no_send);
    RUN(test_tx_busy_blocks_second);
    RUN(test_complete_tx_reenables);
    RUN(test_enable_frame);
    RUN(test_setzero_frame);
    RUN(test_service_drains_rx);

    if (fails == 0) {
        printf("All gimbal_stepbus tests passed.\n");
    }
    return (fails == 0) ? 0 : 1;
}
