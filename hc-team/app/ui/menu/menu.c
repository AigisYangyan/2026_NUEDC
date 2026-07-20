/**
 * @file    menu.c
 * @brief   板载菜单实现——两级导航状态机（GROUP_LIST/RUN_LIST/RUN_ACTIVE）+ 泵送/渲染编排。
 *
 * 两级状态机（转移见下方注释与各处理函数）：
 *   GROUP_LIST --ENTER RUN 组--> RUN_LIST --ENTER 条目--> RUN_ACTIVE --BACK--> RUN_LIST
 *   GROUP_LIST --ENTER PARAM 组--> PARAM_LIST <--BACK--> (menu_param 子状态机) --BACK--> GROUP_LIST
 *   RUN_LIST --BACK--> GROUP_LIST
 * RUN_ACTIVE 期：菜单画统一 RUNNING 横幅（row0）+ 清 row1..3；经 Menu_SetEntrySelfDraw
 * 标记的条目除外——menu 零绘制、整屏归条目服务（W7 §29 显示所有权契约修订 2，
 * 首个使用者 GrayTest 标定助手）；仅 BACK 触发 Scheduler_LeaveEntry。
 *
 * 活动一级分类索引 = GROUP_LIST 光标 s_group_cursor（下探 L2 期间不变）；RUN 组的条目
 * 子列表位 j 经 g->entries[j] 映射为 scheduler 全局条目索引（scheduler 是条目唯一所有者）。
 *
 * 渲染门控：仅当「有待渲染 且 显示就绪」时渲染一次并清待渲染位——
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

/* self-draw 标记集位宽（= s_self_draw_mask 的位数）；登记面与消费面共用防失一致。 */
#define MENU_SELF_DRAW_MASK_BITS 32u

static Menu_Screen         s_screen;
static const Menu_Group_T *s_groups;
static uint8_t             s_group_count;
static uint8_t             s_group_cursor; /* L1 焦点 = 活动一级分类索引 */
static uint8_t             s_run_cursor;   /* L2 运行条目子列表焦点 */
static bool                s_dirty;        /* 有待渲染 */
static uint32_t            s_self_draw_mask; /* bit i = scheduler 条目 i 自绘整屏（W7 opt-in） */

/* 当前活动一级分类（GROUP_LIST 光标所指；调用前 s_group_count>0 由各处理函数保证）。 */
static const Menu_Group_T *active_group(void)
{
    return &s_groups[s_group_cursor];
}

/* 整行项：焦点前缀 + ASCII 文本（hmi 截断到 16 列并空格填满）。
 * text 契约非 NULL：结构体字段名（Menu_Group_T/Menu_Param_T.name）由 menu.h 契约保证；
 * scheduler 条目名是返回值型可空来源（越界返回 NULL），已在 run_entry_name_of 唯一边界收敛。
 * 故此处不逐层兜底——无失败模型的防御分支应删（§8.3）。 */
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

/* 通用列表渲染：标题 + 以 cursor 为焦点的 3 行滚动窗口；name_of(idx) 取第 idx 项名。 */
static void render_list(const char *title, uint8_t total, uint8_t cursor,
                        const char *(*name_of)(uint8_t idx))
{
    uint8_t top;
    uint8_t i;

    (void)Hmi_PrintLine(0u, title);

    if (total == 0u) {
        for (i = 1u; i <= MENU_VISIBLE_ITEMS; ++i) {
            (void)Hmi_PrintLine(i, "");
        }
        return;
    }

    if (cursor >= total) {
        cursor = (uint8_t)(total - 1u);
    }
    top = (uint8_t)((cursor / MENU_VISIBLE_ITEMS) * MENU_VISIBLE_ITEMS);

    for (i = 0u; i < MENU_VISIBLE_ITEMS; ++i) {
        uint8_t idx = (uint8_t)(top + i);
        uint8_t row = (uint8_t)(i + 1u);

        if (idx >= total) {
            (void)Hmi_PrintLine(row, "");
        } else {
            render_item(row, idx == cursor, name_of(idx));
        }
    }
}

static const char *group_name_of(uint8_t idx)
{
    return s_groups[idx].name;
}

static const char *run_entry_name_of(uint8_t idx)
{
    /* scheduler 名字是唯一可空来源：GetEntryName 对越界索引返回 NULL——entries[] 与
     * scheduler 登记表是两张独立维护的表，可能失步（条目数/顺序变更）。在此唯一边界
     * 收敛为占位串，使共享的 render_item 只面对非 NULL；scheduler 仍是名字所有者，
     * menu 不复算（单一所有者）。 */
    const char *name = Scheduler_GetEntryName(active_group()->entries[idx]);

    return (name != NULL) ? name : "?";
}

static void render_group_list(void)
{
    render_list("-- MENU --", s_group_count, s_group_cursor, group_name_of);
}

static void render_run_list(void)
{
    /* 标题用分类名给出上下文（hmi 自截断到 16 列）。 */
    render_list(active_group()->name, active_group()->entry_count, s_run_cursor,
                run_entry_name_of);
}

/* RUN_ACTIVE 统一横幅：menu 占 row0=RUNNING、清 row1..3。经 Menu_SetEntrySelfDraw 标记的
 * 条目除外——menu 零绘制，整屏显示权在条目服务（W7 §29 opt-in）。单写者不变量：单活动条目
 * + 同拍序（Menu_Tick 先行、on_step 在后）保证任意时刻 OLED 写者唯一，无双写冲突。 */
static void render_run_active(void)
{
    int16_t active = Scheduler_GetActiveEntry();
    uint8_t i;

    if ((active >= 0) && ((uint8_t)active < MENU_SELF_DRAW_MASK_BITS) &&
        (((s_self_draw_mask >> (uint8_t)active) & 1u) != 0u)) {
        return; /* self-draw 条目：显示权已让渡，menu 不画横幅、不清行 */
    }

    (void)Hmi_PrintLine(0u, "RUNNING");
    for (i = 1u; i <= MENU_VISIBLE_ITEMS; ++i) {
        (void)Hmi_PrintLine(i, "");
    }
}

/* GROUP_LIST（L1）事件处理；返回是否产生需要重绘的变化。 */
static bool handle_group_list(Hmi_Input ev)
{
    switch (ev) {
    case HMI_INPUT_UP:
        if (s_group_count > 0u) {
            s_group_cursor = (uint8_t)((s_group_cursor + s_group_count - 1u) % s_group_count);
            return true;
        }
        return false;
    case HMI_INPUT_DOWN:
        if (s_group_count > 0u) {
            s_group_cursor = (uint8_t)((s_group_cursor + 1u) % s_group_count);
            return true;
        }
        return false;
    case HMI_INPUT_ENTER:
        if (s_group_count == 0u) {
            return false;
        }
        if (active_group()->kind == MENU_GROUP_PARAM) {
            MenuParam_Enter(active_group()->params, active_group()->param_count);
            s_screen = MENU_SCREEN_PARAM_LIST;
        } else {
            s_run_cursor = 0u;
            s_screen = MENU_SCREEN_RUN_LIST;
        }
        return true;
    case HMI_INPUT_BACK:
    default:
        return false; /* 根界面：BACK 无操作 */
    }
}

/* RUN_LIST（L2）事件处理；返回是否产生需要重绘的变化。 */
static bool handle_run_list(Hmi_Input ev)
{
    uint8_t total = active_group()->entry_count;

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
        /* 子列表位 → scheduler 全局条目索引。进入成功→置脏，渲染统一 RUNNING 横幅。 */
        if (Scheduler_EnterEntry(active_group()->entries[s_run_cursor])) {
            s_screen = MENU_SCREEN_RUN_ACTIVE;
            return true;
        }
        return false;
    case HMI_INPUT_BACK:
        s_screen = MENU_SCREEN_GROUP_LIST;
        return true;
    default:
        return false;
    }
}

/* RUN_ACTIVE 事件处理：仅 BACK 停止并回到本组条目子列表（重绘）；其余丢弃。 */
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
    case MENU_SCREEN_GROUP_LIST:
        if (handle_group_list(ev)) {
            s_dirty = true;
        }
        break;
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
    case MENU_SCREEN_GROUP_LIST:
        render_group_list();
        break;
    case MENU_SCREEN_RUN_LIST:
        render_run_list();
        break;
    case MENU_SCREEN_PARAM_LIST:
    case MENU_SCREEN_PARAM_EDIT:
        MenuParam_Render();
        break;
    case MENU_SCREEN_RUN_ACTIVE:
        render_run_active();
        break;
    default:
        break;
    }
}

void Menu_Setup(const Menu_Group_T *groups, uint8_t group_count)
{
    s_screen = MENU_SCREEN_GROUP_LIST;
    s_groups = groups;
    s_group_count = group_count;
    s_group_cursor = 0u;
    s_run_cursor = 0u;
    s_dirty = true;
    s_self_draw_mask = 0u; /* 重装配即清 opt-in 标记；装配层随后按需重新登记 */
}

void Menu_SetEntrySelfDraw(uint8_t entry_index)
{
    if (entry_index < MENU_SELF_DRAW_MASK_BITS) { /* 越界登记视为装配错误，静默忽略防 UB 移位 */
        s_self_draw_mask |= (uint32_t)1u << entry_index;
    }
}

void Menu_Tick(uint32_t now_ms)
{
    (void)now_ms; /* 预留匹配 background_step 签名；门控归 hmi 自身 */

    Hmi_Update();
    handle_event(Hmi_PollInput());

    if (s_dirty && Hmi_IsDisplayReady()) {
        render();
        s_dirty = false;
    }
}

Menu_Screen Menu_GetScreen(void)
{
    return s_screen;
}
