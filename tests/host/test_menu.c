/**
 * @file    test_menu.c
 * @brief   menu 菜单主机测试（UI01 契约 §13.4 E03，修订 2 两级分类外壳）。
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

/* ---- scheduler 运行条目（钩子全 NULL，活动索引即观测点）---------------------- */
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
    { "P0", p0_get, p0_set, 5, NULL },
    { "P1", p1_get, p1_set, 1, NULL },
    { "P2_CLAMP", p2_get, p2_set, 4, NULL },
};

/* ---- 动作项（PT3）：K3 调回调并停留 PARAM_LIST，不进 EDIT ---------------------- */
static int s_action_calls;
static void act_do(void) { s_action_calls++; }

/* 参数组含 1 普通项 + 1 动作项（SAVE 语义，get/set 为 NULL 证不被调）。 */
static const Menu_Param_T k_action_params[2] = {
    { "P0",   p0_get, p0_set, 5, NULL },   /* 普通项：K3 进 EDIT */
    { "SAVE", NULL,   NULL,   0, act_do }, /* 动作项：K3 调 act_do */
};
static const Menu_Group_T k_action_group[1] = {
    { "ACT", MENU_GROUP_PARAM, NULL, 0u, k_action_params, 2u },
};

/* ---- 一级分类表（装配层职责替身）------------------------------------------- */
/* DEBUG→条目{0,1}；TEST→条目{2,3}；PARAMS→参数表；TASK→条目{1}。
 * 4 类>3 可视 → 触发 GROUP_LIST 滚动窗口；PARAMS 在 index 2。 */
static const uint8_t g_dbg[2]  = { 0u, 1u };
static const uint8_t g_test[2] = { 2u, 3u };
static const uint8_t g_task[1] = { 1u };

static const Menu_Group_T k_groups[4] = {
    { "DEBUG",  MENU_GROUP_RUN,   g_dbg,  2u, NULL,     0u },
    { "TEST",   MENU_GROUP_RUN,   g_test, 2u, NULL,     0u },
    { "PARAMS", MENU_GROUP_PARAM, NULL,   0u, k_params, 3u },
    { "TASK",   MENU_GROUP_RUN,   g_task, 1u, NULL,     0u },
};

/* 空条目运行组（entry_count=0）。 */
static const Menu_Group_T k_empty_run[1] = {
    { "EMPTY", MENU_GROUP_RUN, NULL, 0u, NULL, 0u },
};

/* 失步运行组：entries[] 越界（scheduler 仅 4 条目，索引 9 越界→GetEntryName 返回 NULL）。 */
static const uint8_t g_stale[1] = { 9u };
static const Menu_Group_T k_stale_run[1] = {
    { "STALE", MENU_GROUP_RUN, g_stale, 1u, NULL, 0u },
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
static void setup(const Menu_Group_T *groups, uint8_t group_count)
{
    s_now_ms = 0u;
    FakeI2cPort_Reset();
    FakeBoardGpio_ResetKeys();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    Key_Init();
    Hmi_Init();
    Scheduler_Init(k_entries, 4u, NULL);
    Menu_Setup(groups, group_count);
    FakeBoardGpio_ResetKeyObservability();
}

/* 泵到显示就绪并完成首帧渲染（上电稳定 200ms 后一次 Process）。 */
static void setup_ready(const Menu_Group_T *groups, uint8_t group_count)
{
    setup(groups, group_count);
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

/* 从 GROUP_LIST 进入 PARAMS（index 2）分类并聚焦第 pindex 个参数进入 PARAM_EDIT。 */
static void enter_param_edit(uint8_t pindex)
{
    press(KEY_ID_K2); /* DOWN：DEBUG → TEST */
    press(KEY_ID_K2); /* DOWN：TEST → PARAMS */
    press(KEY_ID_K3); /* ENTER PARAMS → PARAM_LIST（光标 0） */
    for (uint8_t i = 0; i < pindex; ++i) {
        press(KEY_ID_K2); /* DOWN 选参数 */
    }
    press(KEY_ID_K3); /* ENTER → PARAM_EDIT */
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

    Menu_Setup(k_groups, 4u);

    ASSERT_TRUE(FakeI2cPort_GetWriteCount() == writes);
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == transfers);
    ASSERT_EQ_INT(0, FakeBoardGpio_GetKeyLevelReadCount());
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_no_draw_before_ready_then_render(void)
{
    uint32_t transfers;

    setup(k_groups, 4u);

    advance(5u); /* t=5 < 200ms 上电延时：显示未就绪 */
    tick();
    ASSERT_FALSE(Hmi_IsDisplayReady());

    transfers = FakeI2cPort_GetTransferCount();
    advance(200u); /* t=205：泵送完成初始化 + 首帧渲染 GROUP_LIST */
    tick();
    ASSERT_TRUE(Hmi_IsDisplayReady());
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() > transfers);
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_idle_no_redundant_draw(void)
{
    uint32_t transfers;

    setup_ready(k_groups, 4u);
    transfers = FakeI2cPort_GetTransferCount();

    /* 无输入事件的多拍泵送：不重绘 */
    for (int i = 0; i < 6; ++i) {
        advance(5u);
        tick();
    }
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == transfers);
    return true;
}

static bool test_group_list_wraps_and_scrolls(void)
{
    /* 4 分类；UP 从 0 环绕到 3（TASK，越出首窗口页，验证滚动窗口光标可视）。
     * TASK 是 RUN 组 entries{1}，ENTER 子列表首项 → 活动 1。 */
    setup_ready(k_groups, 4u);

    press(KEY_ID_K1); /* UP → 分类光标 3（TASK） */
    press(KEY_ID_K3); /* ENTER TASK → RUN_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    press(KEY_ID_K3); /* ENTER 子列表位 0 → 全局条目 g_task[0]=1 */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(1, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_enter_run_group_opens_sublist(void)
{
    /* GROUP_LIST 光标 0=DEBUG（RUN），ENTER → RUN_LIST（尚未进条目）。 */
    setup_ready(k_groups, 4u);

    press(KEY_ID_K3); /* ENTER DEBUG */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_run_sublist_maps_to_global_entry(void)
{
    /* 进 TEST 组（entries{2,3}），子列表位 1 → 全局条目 3。核心：子列表位→全局索引映射。 */
    setup_ready(k_groups, 4u);

    press(KEY_ID_K2); /* DOWN：DEBUG → TEST */
    press(KEY_ID_K3); /* ENTER TEST → RUN_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());

    press(KEY_ID_K2); /* DOWN 子列表：位 0 → 位 1 */
    press(KEY_ID_K3); /* ENTER → Scheduler_EnterEntry(g_test[1]=3) */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(3, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_run_active_draws_running_once(void)
{
    uint32_t before_enter;
    uint32_t after_enter;

    setup_ready(k_groups, 4u);
    press(KEY_ID_K3); /* ENTER DEBUG → RUN_LIST */
    before_enter = FakeI2cPort_GetTransferCount();

    press(KEY_ID_K3); /* ENTER 条目 0 → RUN_ACTIVE：menu 画统一 RUNNING 横幅一次 */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_ACTIVE, (int)Menu_GetScreen());
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());
    after_enter = FakeI2cPort_GetTransferCount();
    ASSERT_TRUE(after_enter > before_enter); /* RUNNING 横幅绘制发生（旧行为=零绘制，此断言转红） */

    /* 横幅画一次即止：无输入的多拍泵送不重绘（复用 s_dirty 门控） */
    for (int i = 0; i < 8; ++i) {
        advance(5u);
        tick();
    }
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() == after_enter);
    return true;
}

static bool test_back_from_active_returns_to_sublist(void)
{
    uint32_t transfers;

    setup_ready(k_groups, 4u);
    press(KEY_ID_K3); /* ENTER DEBUG → RUN_LIST */
    press(KEY_ID_K3); /* ENTER 条目 0 → RUN_ACTIVE */
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());

    transfers = FakeI2cPort_GetTransferCount();
    press(KEY_ID_K4); /* BACK → LeaveEntry + 回本组 RUN_LIST（非 GROUP_LIST）重绘 */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() > transfers);
    return true;
}

static bool test_back_from_sublist_returns_to_group_list(void)
{
    setup_ready(k_groups, 4u);
    press(KEY_ID_K3); /* ENTER DEBUG → RUN_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    press(KEY_ID_K4); /* BACK → GROUP_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_enter_param_group_opens_param_list(void)
{
    setup_ready(k_groups, 4u);
    press(KEY_ID_K2); /* DOWN：DEBUG → TEST */
    press(KEY_ID_K2); /* DOWN：TEST → PARAMS */
    press(KEY_ID_K3); /* ENTER PARAMS → PARAM_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_edit_up_down_apply_step_live(void)
{
    memset(s_pv, 0, sizeof s_pv);
    setup_ready(k_groups, 4u);
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
    setup_ready(k_groups, 4u);
    enter_param_edit(2u); /* 聚焦 P2_CLAMP，step=4，拥有者上限 10 */

    press(KEY_ID_K1); /* 0→4 */
    ASSERT_EQ_INT(4, (int)s_pv[2]);
    press(KEY_ID_K1); /* 4→8 */
    ASSERT_EQ_INT(8, (int)s_pv[2]);
    press(KEY_ID_K1); /* 8+4=12 → 拥有者夹到 10（菜单不复做限幅） */
    ASSERT_EQ_INT(10, (int)s_pv[2]);
    return true;
}

static bool test_edit_back_steps_out_to_param_list(void)
{
    memset(s_pv, 0, sizeof s_pv);
    setup_ready(k_groups, 4u);
    enter_param_edit(0u);
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_EDIT, (int)Menu_GetScreen());

    press(KEY_ID_K4); /* BACK：EDIT → PARAM_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_back_from_param_list_returns_to_group_list(void)
{
    setup_ready(k_groups, 4u);
    press(KEY_ID_K2); /* DOWN：DEBUG → TEST */
    press(KEY_ID_K2); /* DOWN：TEST → PARAMS */
    press(KEY_ID_K3); /* ENTER PARAMS → PARAM_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());
    press(KEY_ID_K4); /* BACK：PARAM_LIST → GROUP_LIST（修订 2） */
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());
    return true;
}

/* 动作项：PARAM_LIST 上 K3 命中 action 项 → 调回调且停留 PARAM_LIST（不进 EDIT）。
 * SAVE 的 get/set 为 NULL：能正常执行即证动作项路径不触碰 get/set（NULL-safe）。 */
static bool test_action_item_invokes_and_stays_list(void)
{
    s_action_calls = 0;
    setup_ready(k_action_group, 1u);
    press(KEY_ID_K3); /* ENTER ACT → PARAM_LIST（光标 0=P0） */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());

    press(KEY_ID_K2); /* DOWN：光标 0→1（SAVE 动作项） */
    press(KEY_ID_K3); /* ENTER 动作项 → 调 act_do 并停留列表 */
    ASSERT_EQ_INT(1, s_action_calls);
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen()); /* 不进 EDIT */
    return true;
}

/* 动作项可重复触发：每次 K3 一次调用，界面恒停 PARAM_LIST。 */
static bool test_action_item_repeatable(void)
{
    s_action_calls = 0;
    setup_ready(k_action_group, 1u);
    press(KEY_ID_K3); /* ENTER ACT → PARAM_LIST */
    press(KEY_ID_K2); /* → SAVE */
    press(KEY_ID_K3);
    press(KEY_ID_K3);
    press(KEY_ID_K3);
    ASSERT_EQ_INT(3, s_action_calls);
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_LIST, (int)Menu_GetScreen());
    return true;
}

/* 普通项（action==NULL）K3 行为不变：仍进 PARAM_EDIT（回归保护）。 */
static bool test_normal_item_still_enters_edit(void)
{
    s_action_calls = 0;
    setup_ready(k_action_group, 1u);
    press(KEY_ID_K3); /* ENTER ACT → PARAM_LIST（光标 0=P0 普通项） */
    press(KEY_ID_K3); /* ENTER P0 → PARAM_EDIT */
    ASSERT_EQ_INT(MENU_SCREEN_PARAM_EDIT, (int)Menu_GetScreen());
    ASSERT_EQ_INT(0, s_action_calls); /* 普通项不触发 action */
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

static bool test_empty_groups_tick_no_crash(void)
{
    uint32_t transfers;

    setup_ready(NULL, 0u); /* 空分组表 */
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());

    /* ENTER/UP/DOWN/BACK 均安全无副作用，泵送不崩 */
    press(KEY_ID_K3); /* ENTER：group_count=0 → no-op */
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());
    press(KEY_ID_K1);
    press(KEY_ID_K2);
    press(KEY_ID_K4);
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());

    transfers = FakeI2cPort_GetTransferCount();
    ASSERT_TRUE(transfers > 0u); /* 首帧仍渲染了标题 + 空行 */
    return true;
}

static bool test_empty_entry_run_group(void)
{
    /* 空条目运行组：ENTER → RUN_LIST（空子列表）；子列表 ENTER no-op；BACK → GROUP_LIST。 */
    setup_ready(k_empty_run, 1u);
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());

    press(KEY_ID_K3); /* ENTER EMPTY → RUN_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());

    press(KEY_ID_K3); /* ENTER 空子列表 → no-op（不进条目） */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());

    press(KEY_ID_K1); /* UP 环绕：total=0 → no-op */
    press(KEY_ID_K2); /* DOWN：total=0 → no-op */
    press(KEY_ID_K4); /* BACK → GROUP_LIST */
    ASSERT_EQ_INT(MENU_SCREEN_GROUP_LIST, (int)Menu_GetScreen());
    return true;
}

static bool test_run_list_tolerates_stale_entry_index(void)
{
    /* entries[] 与 scheduler 登记表失步：越界索引 → GetEntryName 返回 NULL。渲染须在
     * run_entry_name_of 收敛为占位串、不解引用 NULL（不崩，否则本进程段错误）；
     * screen 保持 RUN_LIST。 */
    uint32_t transfers;

    setup_ready(k_stale_run, 1u);
    transfers = FakeI2cPort_GetTransferCount();

    press(KEY_ID_K3); /* ENTER STALE → RUN_LIST（含越界子列表项，press 的泵送已触发渲染） */
    ASSERT_EQ_INT(MENU_SCREEN_RUN_LIST, (int)Menu_GetScreen());
    ASSERT_TRUE(FakeI2cPort_GetTransferCount() > transfers); /* 渲染发生且未崩 */
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
    run_test("test_group_list_wraps_and_scrolls",
             test_group_list_wraps_and_scrolls);
    run_test("test_enter_run_group_opens_sublist",
             test_enter_run_group_opens_sublist);
    run_test("test_run_sublist_maps_to_global_entry",
             test_run_sublist_maps_to_global_entry);
    run_test("test_run_active_draws_running_once",
             test_run_active_draws_running_once);
    run_test("test_back_from_active_returns_to_sublist",
             test_back_from_active_returns_to_sublist);
    run_test("test_back_from_sublist_returns_to_group_list",
             test_back_from_sublist_returns_to_group_list);
    run_test("test_enter_param_group_opens_param_list",
             test_enter_param_group_opens_param_list);
    run_test("test_edit_up_down_apply_step_live",
             test_edit_up_down_apply_step_live);
    run_test("test_edit_no_menu_clamp_owner_clamps",
             test_edit_no_menu_clamp_owner_clamps);
    run_test("test_edit_back_steps_out_to_param_list",
             test_edit_back_steps_out_to_param_list);
    run_test("test_back_from_param_list_returns_to_group_list",
             test_back_from_param_list_returns_to_group_list);
    run_test("test_action_item_invokes_and_stays_list",
             test_action_item_invokes_and_stays_list);
    run_test("test_action_item_repeatable", test_action_item_repeatable);
    run_test("test_normal_item_still_enters_edit",
             test_normal_item_still_enters_edit);
    run_test("test_format_value_boundaries", test_format_value_boundaries);
    run_test("test_empty_groups_tick_no_crash",
             test_empty_groups_tick_no_crash);
    run_test("test_empty_entry_run_group", test_empty_entry_run_group);
    run_test("test_run_list_tolerates_stale_entry_index",
             test_run_list_tolerates_stale_entry_index);

    if (s_failures != 0) {
        printf("Menu tests failed: %d\n", s_failures);
        return 1;
    }

    printf("\nAll Menu tests passed.\n");
    return 0;
}
