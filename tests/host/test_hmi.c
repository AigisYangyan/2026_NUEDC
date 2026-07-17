/**
 * @file    test_hmi.c
 * @brief   hmi 人机输入/显示服务主机测试（S04 契约 §10.4 E03）。
 *
 * 链接组成：真实 hmi.c + 真实 key.c + 真实 oled_hardware_i2c.c
 *           + fake_board_gpio.c + fake_i2c_port.c。
 * 不链接 fake_clock.c：fake_i2c_port 自带可设定 Clock_NowMs
 * （FakeI2cPort_SetNowMs），时间注入统一走该入口。
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app/service/hmi/hmi.h"
#include "driver/oled/oled_hardware_i2c.h"
#include "driver/key/key.h"

#define KEY_BIT(key) ((uint8_t)(1u << (uint8_t)(key)))

void FakeBoardGpio_ResetKeys(void);
void FakeBoardGpio_SetKeyLevels(uint8_t pressed_bits);
void FakeBoardGpio_SetKeyEdges(uint8_t edge_bits);
void FakeBoardGpio_OrKeyEdges(uint8_t edge_bits);
void FakeBoardGpio_ResetKeyObservability(void);
int FakeBoardGpio_GetKeyLevelReadCount(void);

void FakeI2cPort_Reset(void);
void FakeI2cPort_SetNowMs(uint32_t now_ms);
uint32_t FakeI2cPort_GetWriteCount(void);
uint32_t FakeI2cPort_GetTransferCount(void);

static int s_failures = 0;
static uint32_t s_now_ms = 0u;

#define ASSERT_TRUE(condition)                                                 \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("ASSERT_TRUE failed in %s:%d: %s\n",                        \
                   __func__, __LINE__, #condition);                            \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ_INT(expected, actual)                                        \
    do {                                                                       \
        int expected_value = (expected);                                       \
        int actual_value = (actual);                                           \
        if (expected_value != actual_value) {                                  \
            printf("ASSERT_EQ_INT failed in %s:%d: expected %d, got %d\n",     \
                   __func__, __LINE__, expected_value, actual_value);          \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* System 装配层职责替身：底层 Init 完成后再 Hmi_Init。t=0 起步。 */
static void setup_hmi(void)
{
    s_now_ms = 0u;
    FakeI2cPort_Reset();
    FakeBoardGpio_ResetKeys();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    Key_Init();
    Hmi_Init();
    FakeBoardGpio_ResetKeyObservability();
}

static void advance_and_update(uint32_t delta_ms)
{
    s_now_ms += delta_ms;
    FakeI2cPort_SetNowMs(s_now_ms);
    Hmi_Update();
}

/* OLED 上电稳定延时 200ms 后一次 Process 即完成初始化（test_oled 同款事实）。 */
static void setup_hmi_display_ready(void)
{
    setup_hmi();
    advance_and_update(205u);
}

static bool test_init_is_silent_and_inputs_empty(void)
{
    uint32_t writes;
    uint32_t transfers;

    s_now_ms = 0u;
    FakeI2cPort_Reset();
    FakeBoardGpio_ResetKeys();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    Key_Init();
    FakeBoardGpio_ResetKeyObservability();
    writes = FakeI2cPort_GetWriteCount();
    transfers = FakeI2cPort_GetTransferCount();

    Hmi_Init();

    ASSERT_TRUE(FakeI2cPort_GetWriteCount() == writes);
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == transfers);
    ASSERT_EQ_INT(0, FakeBoardGpio_GetKeyLevelReadCount());
    ASSERT_EQ_INT(HMI_INPUT_NONE, (int)Hmi_PollInput());
    ASSERT_FALSE(Hmi_IsDisplayReady());
    return true;
}

static bool test_update_not_due_no_scan(void)
{
    setup_hmi();
    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    FakeBoardGpio_SetKeyLevels(KEY_BIT(KEY_ID_K1));

    Hmi_Update(); /* t=0，Init 同拍：未到期 */

    ASSERT_EQ_INT(0, FakeBoardGpio_GetKeyLevelReadCount());
    return true;
}

static bool test_update_due_scans_once_per_period(void)
{
    setup_hmi();
    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    FakeBoardGpio_SetKeyLevels(KEY_BIT(KEY_ID_K1));

    advance_and_update(5u); /* t=5 到期：一次扫描（边沿候选态会读电平） */
    ASSERT_EQ_INT(1, FakeBoardGpio_GetKeyLevelReadCount());

    Hmi_Update();           /* 同一 t=5 再调：不到期 */
    ASSERT_EQ_INT(1, FakeBoardGpio_GetKeyLevelReadCount());

    advance_and_update(1u); /* t=6：距上次执行 1ms，不到期 */
    ASSERT_EQ_INT(1, FakeBoardGpio_GetKeyLevelReadCount());

    advance_and_update(4u); /* t=10：到期 */
    ASSERT_EQ_INT(2, FakeBoardGpio_GetKeyLevelReadCount());
    return true;
}

static bool test_k1_maps_to_up_after_debounce(void)
{
    setup_hmi();
    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    FakeBoardGpio_SetKeyLevels(KEY_BIT(KEY_ID_K1));

    for (int i = 0; i < 3; ++i) {
        advance_and_update(5u);
        ASSERT_EQ_INT(HMI_INPUT_NONE, (int)Hmi_PollInput());
    }

    advance_and_update(5u); /* 第 4 拍采样确认按下 */
    ASSERT_EQ_INT(HMI_INPUT_UP, (int)Hmi_PollInput());
    ASSERT_EQ_INT(HMI_INPUT_NONE, (int)Hmi_PollInput());
    return true;
}

static bool test_k2_k3_k4_map_to_down_enter_back(void)
{
    uint8_t bits = (uint8_t)(KEY_BIT(KEY_ID_K2) | KEY_BIT(KEY_ID_K3) |
                             KEY_BIT(KEY_ID_K4));

    setup_hmi();
    FakeBoardGpio_SetKeyEdges(bits);
    FakeBoardGpio_SetKeyLevels(bits);

    for (int i = 0; i < 4; ++i) {
        advance_and_update(5u);
    }

    ASSERT_EQ_INT(HMI_INPUT_DOWN, (int)Hmi_PollInput());
    ASSERT_EQ_INT(HMI_INPUT_ENTER, (int)Hmi_PollInput());
    ASSERT_EQ_INT(HMI_INPUT_BACK, (int)Hmi_PollInput());
    ASSERT_EQ_INT(HMI_INPUT_NONE, (int)Hmi_PollInput());
    return true;
}

static bool test_hold_produces_single_event(void)
{
    setup_hmi();
    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    FakeBoardGpio_SetKeyLevels(KEY_BIT(KEY_ID_K1));

    for (int i = 0; i < 4; ++i) {
        advance_and_update(5u);
    }
    ASSERT_EQ_INT(HMI_INPUT_UP, (int)Hmi_PollInput());

    /* 按住不放 + 期间额外下降沿：不得再出事件 */
    FakeBoardGpio_OrKeyEdges(KEY_BIT(KEY_ID_K1));
    for (int i = 0; i < 3; ++i) {
        advance_and_update(5u);
    }
    ASSERT_EQ_INT(HMI_INPUT_NONE, (int)Hmi_PollInput());
    return true;
}

static bool test_poll_none_without_events(void)
{
    setup_hmi();

    for (int i = 0; i < 4; ++i) {
        advance_and_update(5u);
    }

    ASSERT_EQ_INT(HMI_INPUT_NONE, (int)Hmi_PollInput());
    return true;
}

static bool test_pump_advances_display_to_ready(void)
{
    setup_hmi();

    advance_and_update(5u); /* t=5 < 200ms 上电延时：仍未就绪 */
    ASSERT_FALSE(Hmi_IsDisplayReady());

    advance_and_update(200u); /* t=205：泵送完成初始化 */
    ASSERT_TRUE(Hmi_IsDisplayReady());
    return true;
}

static bool test_print_and_clear_rejected_before_ready(void)
{
    uint32_t writes;

    setup_hmi();
    writes = FakeI2cPort_GetWriteCount();

    ASSERT_FALSE(Hmi_PrintLine(0u, "HI"));
    ASSERT_FALSE(Hmi_ClearDisplay());
    ASSERT_TRUE(FakeI2cPort_GetWriteCount() == writes);
    return true;
}

static bool test_print_line_rejects_bad_row_and_null(void)
{
    uint32_t writes;

    setup_hmi_display_ready();
    writes = FakeI2cPort_GetWriteCount();

    ASSERT_FALSE(Hmi_PrintLine((uint8_t)HMI_DISPLAY_ROWS, "X"));
    ASSERT_FALSE(Hmi_PrintLine(0u, NULL));
    ASSERT_TRUE(FakeI2cPort_GetWriteCount() == writes);
    return true;
}

static bool test_print_line_full_row_ownership(void)
{
    uint32_t base;
    uint32_t delta_full;
    uint32_t delta_short;
    uint32_t delta_long;

    setup_hmi_display_ready();

    base = FakeI2cPort_GetWriteCount();
    ASSERT_TRUE(Hmi_PrintLine(0u, "ABCDEFGHIJKLMNOP")); /* 恰 16 列 */
    delta_full = FakeI2cPort_GetWriteCount() - base;
    ASSERT_TRUE(delta_full > 0u);

    base = FakeI2cPort_GetWriteCount();
    ASSERT_TRUE(Hmi_PrintLine(1u, "AB")); /* 不足：行尾空格填满 */
    delta_short = FakeI2cPort_GetWriteCount() - base;

    base = FakeI2cPort_GetWriteCount();
    ASSERT_TRUE(Hmi_PrintLine(2u, "ABCDEFGHIJKLMNOPQRST")); /* 超长：截断 */
    delta_long = FakeI2cPort_GetWriteCount() - base;

    ASSERT_TRUE(delta_short == delta_full);
    ASSERT_TRUE(delta_long == delta_full);
    return true;
}

static bool test_clear_display_transacts_when_ready(void)
{
    uint32_t transfers;

    setup_hmi_display_ready();
    transfers = FakeI2cPort_GetTransferCount();

    ASSERT_TRUE(Hmi_ClearDisplay());
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() > transfers);
    return true;
}

static void run_test(const char *name, bool (*test_fn)(void))
{
    if (test_fn()) {
        printf("PASS: %s\n", name);
        return;
    }

    s_failures++;
}

int main(void)
{
    run_test("test_init_is_silent_and_inputs_empty",
             test_init_is_silent_and_inputs_empty);
    run_test("test_update_not_due_no_scan", test_update_not_due_no_scan);
    run_test("test_update_due_scans_once_per_period",
             test_update_due_scans_once_per_period);
    run_test("test_k1_maps_to_up_after_debounce",
             test_k1_maps_to_up_after_debounce);
    run_test("test_k2_k3_k4_map_to_down_enter_back",
             test_k2_k3_k4_map_to_down_enter_back);
    run_test("test_hold_produces_single_event",
             test_hold_produces_single_event);
    run_test("test_poll_none_without_events", test_poll_none_without_events);
    run_test("test_pump_advances_display_to_ready",
             test_pump_advances_display_to_ready);
    run_test("test_print_and_clear_rejected_before_ready",
             test_print_and_clear_rejected_before_ready);
    run_test("test_print_line_rejects_bad_row_and_null",
             test_print_line_rejects_bad_row_and_null);
    run_test("test_print_line_full_row_ownership",
             test_print_line_full_row_ownership);
    run_test("test_clear_display_transacts_when_ready",
             test_clear_display_transacts_when_ready);

    if (s_failures != 0) {
        printf("HMI tests failed: %d\n", s_failures);
        return 1;
    }

    printf("\nAll HMI tests passed.\n");
    return 0;
}
