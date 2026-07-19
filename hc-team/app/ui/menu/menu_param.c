/**
 * @file    menu_param.c
 * @brief   参数表子视图实现（PARAM_LIST 浏览 + PARAM_EDIT 就地调整）。
 *
 * 状态机（转移随下方注释交付）：
 *   PARAM_LIST --ENTER(有参数)--> PARAM_EDIT --ENTER/BACK--> PARAM_LIST
 *   PARAM_LIST --BACK--> GROUP_LIST（退回一级分类，由 menu.c 接管）
 * 编辑：UP=set(get()+step)，DOWN=set(get()-step)，调整后回读 get() 由渲染反映；
 *       子模块不存值副本、不限幅——回读即反映拥有 Service 的限幅。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/service/hmi/hmi.h"
#include "app/ui/menu/menu.h"
#include "app/ui/menu/menu_param.h"

/* 标题行 + 3 个可视项行（与 menu.c 运行列表同布局）。 */
#define MENU_PARAM_VISIBLE_ITEMS 3u

static const Menu_Param_T *s_params;
static uint8_t             s_count;
static uint8_t             s_cursor;   /* 列表焦点 = 编辑焦点 */
static bool                s_editing;

/* int32 → 十进制字符串（含负号）；buf 容量须 ≥ 12（-2147483648 + NUL）。 */
void MenuParam_FormatValue(int32_t value, char *buf, size_t cap)
{
    char tmp[12];
    uint8_t n = 0u;
    size_t pos = 0u;
    bool neg = (value < 0);
    /* 经 int64 取绝对值，规避 INT32_MIN 的取负溢出。 */
    uint32_t u = neg ? (uint32_t)(-(int64_t)value) : (uint32_t)value;

    if (u == 0u) {
        tmp[n++] = '0';
    }
    while (u != 0u) {
        tmp[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    }

    if (neg && (pos + 1u < cap)) {
        buf[pos++] = '-';
    }
    while ((n > 0u) && (pos + 1u < cap)) {
        buf[pos++] = tmp[--n];
    }
    buf[pos] = '\0';
}

/* 整行项：焦点前缀 + ASCII 文本（hmi 截断到 16 列并空格填满）。
 * text 由契约保证非 NULL（menu.h 参数名 name 不得为 NULL），不逐层兜底（§8.3）。 */
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

void MenuParam_Enter(const Menu_Param_T *params, uint8_t count)
{
    s_params = params;
    s_count = count;
    s_cursor = 0u;
    s_editing = false;
}

Menu_Screen MenuParam_Handle(Hmi_Input ev)
{
    if (s_editing) {
        const Menu_Param_T *p = &s_params[s_cursor];

        switch (ev) {
        case HMI_INPUT_UP:
            p->set(p->get() + p->step);
            break;
        case HMI_INPUT_DOWN:
            p->set(p->get() - p->step);
            break;
        case HMI_INPUT_ENTER:
        case HMI_INPUT_BACK:
            s_editing = false;
            return MENU_SCREEN_PARAM_LIST;
        default:
            break;
        }
        return MENU_SCREEN_PARAM_EDIT;
    }

    switch (ev) {
    case HMI_INPUT_UP:
        if (s_count > 0u) {
            s_cursor = (uint8_t)((s_cursor + s_count - 1u) % s_count);
        }
        break;
    case HMI_INPUT_DOWN:
        if (s_count > 0u) {
            s_cursor = (uint8_t)((s_cursor + 1u) % s_count);
        }
        break;
    case HMI_INPUT_ENTER:
        if (s_count > 0u) {
            const Menu_Param_T *p = &s_params[s_cursor];
            if (p->action != NULL) {
                p->action();                 /* 动作项（如 SAVE）：执行即停留列表，不进编辑 */
                return MENU_SCREEN_PARAM_LIST;
            }
            s_editing = true;
            return MENU_SCREEN_PARAM_EDIT;
        }
        break;
    case HMI_INPUT_BACK:
        return MENU_SCREEN_GROUP_LIST;
    default:
        break;
    }
    return MENU_SCREEN_PARAM_LIST;
}

static void render_edit(void)
{
    const Menu_Param_T *p = &s_params[s_cursor];
    char value[12];

    (void)Hmi_PrintLine(0u, "-- EDIT --");
    (void)Hmi_PrintLine(1u, p->name);
    MenuParam_FormatValue(p->get(), value, sizeof value);
    (void)Hmi_PrintLine(2u, value);
    (void)Hmi_PrintLine(3u, "UP/DN  BACK");
}

static void render_list(void)
{
    uint8_t top;
    uint8_t i;

    (void)Hmi_PrintLine(0u, "-- PARAMS --");

    if (s_count == 0u) {
        for (i = 1u; i <= MENU_PARAM_VISIBLE_ITEMS; ++i) {
            (void)Hmi_PrintLine(i, "");
        }
        return;
    }

    if (s_cursor >= s_count) {
        s_cursor = (uint8_t)(s_count - 1u);
    }
    top = (uint8_t)((s_cursor / MENU_PARAM_VISIBLE_ITEMS) * MENU_PARAM_VISIBLE_ITEMS);

    for (i = 0u; i < MENU_PARAM_VISIBLE_ITEMS; ++i) {
        uint8_t idx = (uint8_t)(top + i);
        uint8_t row = (uint8_t)(i + 1u);

        if (idx >= s_count) {
            (void)Hmi_PrintLine(row, "");
        } else {
            render_item(row, idx == s_cursor, s_params[idx].name);
        }
    }
}

void MenuParam_Render(void)
{
    if (s_editing) {
        render_edit();
    } else {
        render_list();
    }
}
