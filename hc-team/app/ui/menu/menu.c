/**
 * @file    menu.c
 * @brief   板载菜单实现——顶层界面状态机 + 分问选择（RUN_LIST/RUN_ACTIVE）+ 泵送/渲染编排。
 *
 * 顶层状态机（转移见下方注释与各处理函数）：
 *   RUN_LIST --ENTER 条目--> RUN_ACTIVE --BACK--> RUN_LIST
 *   RUN_LIST --ENTER Params--> PARAM_LIST <--BACK--> (menu_param 子状态机)
 * RUN_ACTIVE 期：菜单不写任何显示行——整屏显示所有权归激活条目的 on_step
 * （避免双写者冲突，与 V21 双泵同构的显示所有权隔离）；仅 BACK 触发 Scheduler_LeaveEntry。
 *
 * 渲染门控：仅当「有待渲染 且 非 RUN_ACTIVE 且 显示就绪」时渲染一次并清待渲染位——
 * 无输入事件即不重绘（避免冗余 I2C 事务）；显示未就绪则保持待渲染，就绪后补绘。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/scheduler/scheduler.h"
#include "app/service/hmi/hmi.h"
#include "app/ui/menu/menu.h"
#include "app/ui/menu/menu_param.h"

/* 标题行 + 3 个可视项行（64px / 16px 字模 = 4 行）。 */
#define MENU_VISIBLE_ITEMS 3u

static Menu_Screen s_screen;
static uint8_t     s_run_cursor;   /* RUN_LIST 焦点：0..entry_count-1 = 条目，entry_count = Params 尾项 */
static uint8_t     s_param_count;  /* >0 时 RUN_LIST 追加 Params 尾项 */
static bool        s_dirty;        /* 有待渲染 */

/* RUN_LIST 总项数 = 条目数 +（有参数时）Params 尾项。 */
static uint8_t run_list_total(void)
{
    uint8_t total = Scheduler_GetEntryCount();

    if (s_param_count > 0u) {
        total = (uint8_t)(total + 1u);
    }
    return total;
}

/* 整行项：焦点前缀 + ASCII 文本（hmi 截断到 16 列并空格填满）。
 * text 由契约保证非 NULL（scheduler.h 条目名 / menu.h 参数名 / "Params" 字面量），
 * 不在此逐层兜底——契约破坏应在拥有者侧断言（§8.3：无失败模型的防御分支应删）。 */
static void render_item(uint8_t row, bool focused, const char *text)
{
    char buf[HMI_DISPLAY_COLS + 1u];
    uint8_t pos = 0u;

    buf[pos++] = focused ? '>' : ' ';
    while ((*text != '\0') && (pos < HMI_DISPLAY_COLS)) {
        buf[pos++] = *text++;
    }
    buf[pos] = '\0';
    (void)Hmi_PrintLine(row, buf);
}

static void render_run_list(void)
{
    uint8_t entry_count = Scheduler_GetEntryCount();
    uint8_t total = run_list_total();
    uint8_t top;
    uint8_t i;

    (void)Hmi_PrintLine(0u, "-- SELECT --");

    if (total == 0u) {
        for (i = 1u; i <= MENU_VISIBLE_ITEMS; ++i) {
            (void)Hmi_PrintLine(i, "");
        }
        return;
    }

    if (s_run_cursor >= total) {
        s_run_cursor = (uint8_t)(total - 1u);
    }
    top = (uint8_t)((s_run_cursor / MENU_VISIBLE_ITEMS) * MENU_VISIBLE_ITEMS);

    for (i = 0u; i < MENU_VISIBLE_ITEMS; ++i) {
        uint8_t idx = (uint8_t)(top + i);
        uint8_t row = (uint8_t)(i + 1u);
        const char *name;

        if (idx >= total) {
            (void)Hmi_PrintLine(row, "");
            continue;
        }
        name = (idx < entry_count) ? Scheduler_GetEntryName(idx) : "Params";
        render_item(row, idx == s_run_cursor, name);
    }
}

/* RUN_LIST 事件处理；返回是否产生了需要重绘的变化。 */
static bool handle_run_list(Hmi_Input ev)
{
    uint8_t entry_count = Scheduler_GetEntryCount();
    uint8_t total = run_list_total();

    switch (ev) {
    case HMI_INPUT_UP:
        if (total > 0u) {
            s_run_cursor = (uint8_t)((s_run_cursor + total - 1u) % total);
            return true;
        }
        return false;
    case HMI_INPUT_DOWN:
        if (total > 0u) {
            s_run_cursor = (uint8_t)((s_run_cursor + 1u) % total);
            return true;
        }
        return false;
    case HMI_INPUT_ENTER:
        if (total == 0u) {
            return false;
        }
        if (s_run_cursor < entry_count) {
            if (Scheduler_EnterEntry(s_run_cursor)) {
                s_screen = MENU_SCREEN_RUN_ACTIVE; /* 让出显示，无待渲染 */
                return false;
            }
            return false;
        }
        /* 焦点在 Params 尾项。 */
        MenuParam_Enter();
        s_screen = MENU_SCREEN_PARAM_LIST;
        return true;
    case HMI_INPUT_BACK:
    default:
        return false; /* 根界面：BACK 无操作 */
    }
}

/* RUN_ACTIVE 事件处理：仅 BACK 停止并回到运行列表（重绘）；其余丢弃。 */
static bool handle_run_active(Hmi_Input ev)
{
    if (ev == HMI_INPUT_BACK) {
        Scheduler_LeaveEntry();
        s_screen = MENU_SCREEN_RUN_LIST;
        return true;
    }
    return false;
}

static void handle_event(Hmi_Input ev)
{
    switch (s_screen) {
    case MENU_SCREEN_RUN_LIST:
        if (handle_run_list(ev)) {
            s_dirty = true;
        }
        break;
    case MENU_SCREEN_RUN_ACTIVE:
        if (handle_run_active(ev)) {
            s_dirty = true;
        }
        break;
    case MENU_SCREEN_PARAM_LIST:
    case MENU_SCREEN_PARAM_EDIT:
        s_screen = MenuParam_Handle(ev);
        if (ev != HMI_INPUT_NONE) {
            s_dirty = true;
        }
        break;
    default:
        break;
    }
}

static void render(void)
{
    switch (s_screen) {
    case MENU_SCREEN_RUN_LIST:
        render_run_list();
        break;
    case MENU_SCREEN_PARAM_LIST:
    case MENU_SCREEN_PARAM_EDIT:
        MenuParam_Render();
        break;
    case MENU_SCREEN_RUN_ACTIVE:
    default:
        break; /* 让出显示；渲染门控已排除，此处不触及 */
    }
}

void Menu_Setup(const Menu_Param_T *params, uint8_t param_count)
{
    s_screen = MENU_SCREEN_RUN_LIST;
    s_run_cursor = 0u;
    s_param_count = param_count;
    s_dirty = true;
    MenuParam_Init(params, param_count);
}

void Menu_Tick(uint32_t now_ms)
{
    (void)now_ms; /* 预留匹配 background_step 签名；门控归 hmi 自身 */

    Hmi_Update();
    handle_event(Hmi_PollInput());

    if (s_dirty && (s_screen != MENU_SCREEN_RUN_ACTIVE) && Hmi_IsDisplayReady()) {
        render();
        s_dirty = false;
    }
}

Menu_Screen Menu_GetScreen(void)
{
    return s_screen;
}
