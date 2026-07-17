/**
 * @file    test_menu.c
 * @brief   menu 菜单主机测试（UI01 契约 §13.4 E03）。
 *
 * 链接组成：真实 menu.c + menu_param.c + hmi.c + key.c + oled_hardware_i2c.c
 *           + scheduler.c + fake_board_gpio.c + fake_i2c_port.c。
 * 不链接 fake_clock.c：fake_i2c_port 自带可设定 Clock_NowMs（FakeI2cPort_SetNowMs）。
 *
 * 观测手段（避免脆弱的 OLED 字模解码）：
 * - 界面 = Menu_GetScreen()；运行光标经 ENTER→Scheduler_GetActiveEntry() 间接观测；
 * - 绘制/不绘制 = FakeI2cPort_GetTransferCount() 增量；
 * - 参数值 = 测试内 backing 变量；格式化 = 直接调 MenuParam_FormatValue。
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/scheduler/scheduler.h"
#include "app/service/hmi/hmi.h"
#include "app/ui/menu/menu.h"
#include "app/ui/menu/menu_param.h"
#include "driver/key/key.h"
#include "driver/oled/oled_hardware_i2c.h"

#define KEY_BIT(key) ((uint8_t)(1u << (uint8_t)(key)))

void FakeBoardGpio_ResetKeys(void);
void FakeBoardGpio_SetKeyLevels(uint8_t pressed_bits);
void FakeBoardGpio_SetKeyEdges(uint8_t edge_bits);
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

#define ASSERT_STR_EQ(expected, actual)                                        \
    do {                                                                       \
        if (strcmp((expected), (actual)) != 0) {                               \
            printf("ASSERT_STR_EQ failed in %s:%d: expected \"%s\", got "      \
                   "\"%s\"\n", __func__, __LINE__, (expected), (actual));      \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* ---- 运行条目（钩子全 NULL，活动索引即观测点）------------------------------ */
static const Scheduler_Entry_T k_entries[4] = {
    { "RUN0", NULL, NULL, NULL },
    { "RUN1", NULL, NULL, NULL },
    { "RUN2", NULL, NULL, NULL },
    { "RUN3", NULL, NULL, NULL },
};

/* ---- 参数表（backing 变量 + accessor）------------------------------------- */
static int32_t s_pv[3];

static int32_t p0_get(void) { return s_pv[0]; }
static void p0_set(int32_t v) { s_pv[0] = v; }
static int32_t p1_get(void) { return s_pv[1]; }
static void p1_set(int32_t v) { s_pv[1] = v; }

/* 拥有者限幅 accessor：上限 10（演示菜单不复做限幅，回读反映拥有者限幅）。 */
static int32_t p2_get(void) { return s_pv[2]; }
static void p2_set(int32_t v)
{
    if (v > 10) {
        v = 10;
    }
    s_pv[2] = v;
}

static const Menu_Param_T k_params[3] = {
    { "P0", p0_get, p0_set, 5 },
    { "P1", p1_get, p1_set, 1 },
    { "P2_CLAMP", p2_get, p2_set, 4 },
};

/* ---- 泵送与输入驱动 -------------------------------------------------------- */
static void advance(uint32_t delta_ms)
{
    s_now_ms += delta_ms;
    FakeI2cPort_SetNowMs(s_now_ms);
}

static void tick(void)
{
    Menu_Tick(s_now_ms);
}

/* 完整装配：底层 Init → Scheduler_Init → Menu_Setup（System 装配层职责替身）。 */
static void setup(const Scheduler_Entry_T *entries, uint8_t entry_count,
                  const Menu_Param_T *params, uint8_t param_count)
{
    s_now_ms = 0u;
    FakeI2cPort_Reset();
    FakeBoardGpio_ResetKeys();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    Key_Init();
    Hmi_Init();
    Scheduler_Init(entries, entry_count, NULL);
    Menu_Setup(params, param_count);
    FakeBoardGpio_ResetKeyObservability();
}

/* 泵到显示就绪并完成首帧渲染（上电稳定 200ms 后一次 Process）。 */
static void setup_ready(const Scheduler_Entry_T *entries, uint8_t entry_count,
                        const Menu_Param_T *params, uint8_t param_count)
{
    setup(entries, entry_count, params, param_count);
    advance(205u);
    tick();
}

/*
 * 注入一次去抖按下并经菜单消费。key.c 模型：按下需连续 4 拍低电平确认（事件），
 * 释放需连续 4 拍高电平确认（解锁 stable_pressed）才允许下一次事件。故：
 * 保持 5 拍（第 4 拍出事件，同拍被 Menu_Tick 消费），再释放 6 拍彻底解锁。
 */
static void press(Key_Id_e key)
{
    uint8_t bit = KEY_BIT(key);
    int i;

    FakeBoardGpio_SetKeyEdges(bit); /* 边沿只需一次；首拍 Key_Scan 消费后清零 */
    FakeBoardGpio_SetKeyLevels(bit);
    for (i = 0; i < 5; ++i) {
        advance(5u);
        tick();
    }
    FakeBoardGpio_SetKeyLevels(0u); /* 释放：连续高电平采样满 4 拍解锁 */
    for (i = 0; i < 6; ++i) {
        advance(5u);
        tick();
    }
}

/* ---- 用例 ------------------------------------------------------------------ */

static bool test_init_is_silent(void)
{
    uint32_t writes;
    uint32_t transfers;

    s_now_ms = 0u;
    FakeI2cPort_Reset();
    FakeBoardGpio_ResetKeys();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    Key_Init();
    Hmi_Init();
    Scheduler_Init(k_entries, 4u, NULL);
    FakeBoardGpio_ResetKeyObservability();
    writes = FakeI2cPort_GetWriteCount();
    transfers = FakeI2cPort_GetTransferCount();

    Menu_Setup(k_params, 3u);

    ASSERT_TRUE(FakeI2cPort_GetWriteCount() == writes);
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == transfers);
    ASSERT_EQ_INT(0, FakeBoardGpio_GetKeyLevelReadCount());
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_no_draw_before_ready_then_render(void)
{
    uint32_t transfers;

    setup(k_entries, 4u, k_params, 3u);

    advance(5u); /* t=5 < 200ms 上电延时：显示未就绪 */
    tick();
    ASSERT_FALSE(Hmi_IsDisplayReady());

    transfers = FakeI2cPort_GetTransferCount();
    advance(200u); /* t=205：泵送完成初始化 + 首帧渲染 RUN_LIST */
    tick();
    ASSERT_TRUE(Hmi_IsDisplayReady());
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() > transfers);
    return true;
}

static bool test_idle_no_redundant_draw(void)
{
    uint32_t transfers;

    setup_ready(k_entries, 4u, k_params, 3u);
    transfers = FakeI2cPort_GetTransferCount();

    /* 无输入事件的多拍泵送：不重绘 */
    for (int i = 0; i < 6; ++i) {
        advance(5u);
        tick();
    }
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == transfers);
    return true;
}

static bool test_cursor_down_selects_indexed_entry(void)
{
    /* 4 条目 + 3 参数 → total=5；DOWN×2 落在条目 2（越过首窗口内），ENTER→活动 2。 */
    setup_ready(k_entries, 4u, k_params, 3u);

    press(KEY_ID_K2); /* DOWN → 光标 1 */
    press(KEY_ID_K2); /* DOWN → 光标 2 */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());

    press(KEY_ID_K3); /* ENTER */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(2, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_cursor_wraps_up_to_last_entry(void)
{
    /* 无参数 → total=entry_count=4；UP 从 0 环绕到 3；ENTER→活动 3（验证越窗口光标）。 */
    setup_ready(k_entries, 4u, NULL, 0u);

    press(KEY_ID_K1); /* UP → 光标 3（末条目，第 2 窗口页） */
    press(KEY_ID_K3); /* ENTER */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(3, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_run_active_menu_draws_nothing(void)
{
    uint32_t transfers;

    setup_ready(k_entries, 4u, k_params, 3u);
    press(KEY_ID_K3); /* ENTER 条目 0 → RUN_ACTIVE */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());

    /* 激活期菜单让出整屏：多拍泵送（无条目绘制）→ 零 I2C 事务 */
    transfers = FakeI2cPort_GetTransferCount();
    for (int i = 0; i < 8; ++i) {
        advance(5u);
        tick();
    }
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == transfers);
    return true;
}

static bool test_back_from_active_leaves_and_redraws(void)
{
    uint32_t transfers;

    setup_ready(k_entries, 4u, k_params, 3u);
    press(KEY_ID_K3); /* ENTER → 活动 0 */
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());

    transfers = FakeI2cPort_GetTransferCount();
    press(KEY_ID_K4); /* BACK → LeaveEntry + 回 RUN_LIST 重绘 */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() > transfers);
    return true;
}

static bool test_params_tail_present_only_with_params(void)
{
    /* 有参数：1 条目 + Params 尾项 → total=2；DOWN 到尾项 ENTER→PARAM_LIST。 */
    setup_ready(k_entries, 1u, k_params, 3u);
    press(KEY_ID_K2); /* DOWN → Params 尾项 */
    press(KEY_ID_K3); /* ENTER Params */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_no_params_tail_when_empty(void)
{
    /* 无参数：1 条目 → total=1；DOWN 环绕仍在条目 0；ENTER→活动条目（非 PARAM）。 */
    setup_ready(k_entries, 1u, NULL, 0u);
    press(KEY_ID_K2); /* DOWN → 环绕回条目 0（无 Params 尾项） */
    press(KEY_ID_K3); /* ENTER */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());
    return true;
}

/* 从 RUN_LIST（1 条目 + 参数）进入 PARAM_EDIT 聚焦第 index 个参数。 */
static void enter_param_edit(uint8_t index)
{
    press(KEY_ID_K2); /* DOWN：条目 0 → Params 尾项 */
    press(KEY_ID_K3); /* ENTER Params → PARAM_LIST（光标 0） */
    for (uint8_t i = 0; i < index; ++i) {
        press(KEY_ID_K2); /* DOWN 选参数 */
    }
    press(KEY_ID_K3); /* ENTER → PARAM_EDIT */
}

static bool test_edit_up_down_apply_step_live(void)
{
    memset(s_pv, 0, sizeof s_pv);
    setup_ready(k_entries, 1u, k_params, 3u);
    enter_param_edit(0u); /* 聚焦 P0，step=5 */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_EDIT, (int)Menu_GetScreen());

    press(KEY_ID_K1); /* UP → set(get()+5) */
    ASSERT_EQ_INT(5, (int)s_pv[0]);
    press(KEY_ID_K1); /* UP → 10 */
    ASSERT_EQ_INT(10, (int)s_pv[0]);
    press(KEY_ID_K2); /* DOWN → set(get()-5) → 5 */
    ASSERT_EQ_INT(5, (int)s_pv[0]);

    /* 菜单不存值副本：外部改 backing 后 UP 读到的是 live 值 */
    s_pv[0] = 500;
    press(KEY_ID_K1); /* set(get()+5) = 505 */
    ASSERT_EQ_INT(505, (int)s_pv[0]);
    return true;
}

static bool test_edit_no_menu_clamp_owner_clamps(void)
{
    memset(s_pv, 0, sizeof s_pv);
    setup_ready(k_entries, 1u, k_params, 3u);
    enter_param_edit(2u); /* 聚焦 P2_CLAMP，step=4，拥有者上限 10 */

    press(KEY_ID_K1); /* 0→4 */
    ASSERT_EQ_INT(4, (int)s_pv[2]);
    press(KEY_ID_K1); /* 4→8 */
    ASSERT_EQ_INT(8, (int)s_pv[2]);
    press(KEY_ID_K1); /* 8+4=12 → 拥有者夹到 10（菜单不复做限幅） */
    ASSERT_EQ_INT(10, (int)s_pv[2]);
    return true;
}

static bool test_edit_back_steps_out(void)
{
    memset(s_pv, 0, sizeof s_pv);
    setup_ready(k_entries, 1u, k_params, 3u);
    enter_param_edit(0u);
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_EDIT, (int)Menu_GetScreen());

    press(KEY_ID_K4); /* BACK：EDIT → PARAM_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());
    press(KEY_ID_K4); /* BACK：PARAM_LIST → RUN_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_format_value_boundaries(void)
{
    char buf[12];

    MenuParam_FormatValue(0, buf, sizeof buf);
    ASSERT_STR_EQ("0", buf);
    MenuParam_FormatValue(7, buf, sizeof buf);
    ASSERT_STR_EQ("7", buf);
    MenuParam_FormatValue(-42, buf, sizeof buf);
    ASSERT_STR_EQ("-42", buf);
    MenuParam_FormatValue(123456, buf, sizeof buf);
    ASSERT_STR_EQ("123456", buf);
    MenuParam_FormatValue(2147483647, buf, sizeof buf);
    ASSERT_STR_EQ("2147483647", buf);
    MenuParam_FormatValue((int32_t)(-2147483647 - 1), buf, sizeof buf);
    ASSERT_STR_EQ("-2147483648", buf); /* INT32_MIN：int64 取负无溢出 */
    return true;
}

static bool test_empty_tables_tick_no_crash(void)
{
    uint32_t transfers;

    setup_ready(NULL, 0u, NULL, 0u); /* 空条目表 + 空参数表 */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());

    /* ENTER/UP/DOWN/BACK 均安全无副作用，泵送不崩 */
    press(KEY_ID_K3); /* ENTER：total=0 → no-op */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    press(KEY_ID_K1);
    press(KEY_ID_K2);
    press(KEY_ID_K4);
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());

    transfers = FakeI2cPort_GetTransferCount();
    ASSERT_TRUE(transfers > 0u); /* 首帧仍渲染了标题+空行 */
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
    run_test("test_init_is_silent", test_init_is_silent);
    run_test("test_no_draw_before_ready_then_render",
             test_no_draw_before_ready_then_render);
    run_test("test_idle_no_redundant_draw", test_idle_no_redundant_draw);
    run_test("test_cursor_down_selects_indexed_entry",
             test_cursor_down_selects_indexed_entry);
    run_test("test_cursor_wraps_up_to_last_entry",
             test_cursor_wraps_up_to_last_entry);
    run_test("test_run_active_menu_draws_nothing",
             test_run_active_menu_draws_nothing);
    run_test("test_back_from_active_leaves_and_redraws",
             test_back_from_active_leaves_and_redraws);
    run_test("test_params_tail_present_only_with_params",
             test_params_tail_present_only_with_params);
    run_test("test_no_params_tail_when_empty", test_no_params_tail_when_empty);
    run_test("test_edit_up_down_apply_step_live",
             test_edit_up_down_apply_step_live);
    run_test("test_edit_no_menu_clamp_owner_clamps",
             test_edit_no_menu_clamp_owner_clamps);
    run_test("test_edit_back_steps_out", test_edit_back_steps_out);
    run_test("test_format_value_boundaries", test_format_value_boundaries);
    run_test("test_empty_tables_tick_no_crash",
             test_empty_tables_tick_no_crash);

    if (s_failures != 0) {
        printf("Menu tests failed: %d\n", s_failures);
        return 1;
    }

    printf("\nAll Menu tests passed.\n");
    return 0;
}
