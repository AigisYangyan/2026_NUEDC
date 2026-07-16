/**
 * @file    menu_core.c
 * @brief   轻量注册式 OLED 菜单核心实现
 *
 * 本文件实现菜单状态管理、按键映射、页面跳转与默认列表渲染。
 *
 * 功能范围：
 * - 维护菜单当前页面、光标与可视窗口状态
 * - 将物理按键映射为菜单动作
 * - 提供默认列表渲染与按需刷新接口
 *
 * 设计约定：
 * - 菜单核心负责显示、导航与页面级返回链路
 * - 页面注册表由 menu_pages.c 提供
 * - 页面内容未变化时不触发 OLED 重绘
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app/ui/oled/menu_core.h"
#include "app/ui/oled/menu_pages.h"
#include "app/scheduler/task_scheduler.h"
#include "driver/oled/oled_hardware_i2c.h"

 /* ---- 布局常量 ----------------------------------------------------------- */

#define MENU_TITLE_Y_PAGE        0u//标题行页地址
#define MENU_FIRST_ITEM_Y_PAGE   2u//第一行菜单项页地址
#define MENU_PAGE_ROW_STEP       2u//菜单项行距（页地址增量）
#define MENU_VISIBLE_ITEM_COUNT  3u//可见菜单项数量（根据 OLED 行数与行距计算得出）
#define MENU_CURSOR_X            0u//光标 X 坐标（列地址）
#define MENU_TEXT_X              16u//菜单文本 X 坐标（列地址）2

/* ---- 模块状态 ----------------------------------------------------------- */

static MenuState_t s_tMenuState = {
    PAGE_HOME,
    0u,
    0u,
    true
};

/* ---- 静态辅助函数 ------------------------------------------------------- */
// 将物理按键映射为菜单动作
static MenuKeyAction_e menu_map_key_to_action(Key_Id_e key)
{
    switch (key) {
    case KEY_ID_K1:
        return MENU_KEY_UP;
    case KEY_ID_K2:
        return MENU_KEY_DOWN;
    case KEY_ID_K3:
        return MENU_KEY_ENTER;
    case KEY_ID_K4:
        return MENU_KEY_BACK;
    default:
        return MENU_KEY_NONE;
    }
}

// 根据页面 ID 查找页面描述符，若未找到则返回 NULL
static const MenuPage_t* menu_find_page(MenuPageId_e id)
{
    // 线性搜索页面注册表，考虑到页面数量较少且只在按键事件时调用，性能影响可接受
    for (uint8_t i = 0u; i < g_menu_page_count; i++) {
        if (g_menu_pages[i].id == id) {
            return &g_menu_pages[i];
        }// 继续搜索以防止页面 ID 重复注册，虽然不应发生但增加鲁棒性
    }

    return (const MenuPage_t*)0;//未找到匹配页面
}

// 获取当前系统状态对应的任务组指针，若状态非法则返回空指针
static void menu_draw_string(uint8_t x, uint8_t y, const char* text)
{
    (void)OLED_ShowString(x, y, text, 16u);//默认使用 16 像素高的字体
}

// 获取当前页面描述符，若当前页面 ID 无效则返回 NULL
static void menu_reset_page_view(void)
{
    s_tMenuState.cursor = 0u;
    s_tMenuState.top_index = 0u;
    s_tMenuState.dirty = true;
}// 切换页面时重置光标与窗口状态，并标脏以触发重绘

// 同步光标与窗口状态，确保光标始终在可见范围内
static void menu_sync_scroll_window(const MenuPage_t* page)
{
    //如果页面无效或无菜单项，则重置光标与窗口状态
    if ((page == (const MenuPage_t*)0) || (page->item_count == 0u)) {
        s_tMenuState.cursor = 0u;
        s_tMenuState.top_index = 0u;
        return;
    }
    //如果光标越界则修正，虽然正常操作不应发生但增加鲁棒性
    if (s_tMenuState.cursor >= page->item_count) {
        s_tMenuState.cursor = (uint8_t)(page->item_count - 1u);
    }
    //如果光标在窗口上方则将窗口上移，如果在窗口下方则将窗口下移，保持光标可见
    if (s_tMenuState.cursor < s_tMenuState.top_index) {
        s_tMenuState.top_index = s_tMenuState.cursor;
    }
    //如果光标在窗口下方则将窗口下移，保持光标可见
    if (s_tMenuState.cursor >=
        (uint8_t)(s_tMenuState.top_index + MENU_VISIBLE_ITEM_COUNT)) {
        s_tMenuState.top_index =
            (uint8_t)(s_tMenuState.cursor - MENU_VISIBLE_ITEM_COUNT + 1u);
    }
}

// 切换到指定页面 ID 的页面，若页面 ID 无效则不执行任何操作
static void menu_switch_page(MenuPageId_e next_page)
{
    if (menu_find_page(next_page) == (const MenuPage_t*)0) {
        return;
    }

    s_tMenuState.current_page = next_page;
    menu_reset_page_view();
}

static void menu_return_home_to_idle_page(void)
{
    Sys_LeaveRunEntry();
    menu_switch_page(PAGE_HOME);
}

// 默认页面渲染函数，显示标题与菜单项列表，并在光标位置显示 '>' 符号
static void menu_render_default_page(const MenuPage_t* page,
    const MenuState_t* state)
{
    //如果页面无效则不执行任何渲染操作
    uint8_t visible_count;

    //渲染标题
    if (page == (const MenuPage_t*)0) {
        return;
    }

    //默认渲染：标题 + 菜单项列表 + 光标
    menu_draw_string(0u, MENU_TITLE_Y_PAGE, page->title);

    //如果页面无菜单项则显示提示文本并返回
    if (page->item_count == 0u) {
        menu_draw_string(0u, MENU_FIRST_ITEM_Y_PAGE, "No Items");
        return;
    }

    //计算可见菜单项数量，确保不超过最大可见数量
    visible_count = (page->item_count > state->top_index)
        ? (uint8_t)(page->item_count - state->top_index)
        : 0u;

    if (visible_count > MENU_VISIBLE_ITEM_COUNT) {
        visible_count = MENU_VISIBLE_ITEM_COUNT;
    }

    //渲染菜单项列表与光标
    for (uint8_t i = 0u; i < visible_count; i++) {
        uint8_t y_page = (uint8_t)(MENU_FIRST_ITEM_Y_PAGE + i * MENU_PAGE_ROW_STEP);
        uint8_t item_index = (uint8_t)(state->top_index + i);

        //渲染光标
        if (item_index == state->cursor) {
            (void)OLED_ShowChar(MENU_CURSOR_X, y_page, '>', 16u);
        }

        //渲染菜单项文本
        menu_draw_string(MENU_TEXT_X, y_page, page->items[item_index].text);
    }
}

/* ---- 公开 API ----------------------------------------------------------- */

void Menu_Init(void)
{
    MenuPages_Init();
    s_tMenuState.current_page = PAGE_HOME;
    s_tMenuState.cursor = 0u;
    s_tMenuState.top_index = 0u;
    s_tMenuState.dirty = true;
}

void Menu_SetCurrentPage(MenuPageId_e page_id)
{
    menu_switch_page(page_id);
}

// 处理菜单按键输入，根据当前页面状态执行相应的导航或操作
void Menu_HandleKey(Key_Id_e key)
{
    //将物理按键映射为菜单动作，如果无效则不执行任何操作
    const MenuPage_t* page = Menu_GetCurrentPage();
    MenuKeyAction_e action = menu_map_key_to_action(key);

    //如果页面无效或按键无对应动作则不执行任何操作
    if ((page == (const MenuPage_t*)0) || (action == MENU_KEY_NONE)) {
        return;
    }

    //根据菜单动作执行相应的导航或操作，执行后根据需要标脏以触发重绘
    switch (action) {
    case MENU_KEY_UP:
        if ((page->item_count > 0u) && (s_tMenuState.cursor > 0u)) {
            s_tMenuState.cursor--;
            menu_sync_scroll_window(page);
            Menu_RequestRedraw();
        }
        break;

        //如果按下向下键且光标未在最后一个菜单项，则将光标下移一行，并同步窗口状态以保持光标可见，最后请求重绘
    case MENU_KEY_DOWN:
        if ((page->item_count > 0u) &&
            (s_tMenuState.cursor + 1u < page->item_count)) {
            s_tMenuState.cursor++;
            menu_sync_scroll_window(page);
            Menu_RequestRedraw();
        }
        break;

        //如果按下确认键且当前页面有菜单项，则根据当前光标位置的菜单项类型执行相应操作：如果是子页面跳转则切换到目标页面，如果是返回则切换到父页面，如果是动作则执行对应函数，最后根据需要标脏以触发重绘
    case MENU_KEY_ENTER:
        if ((page->item_count > 0u) && (page->items != (const MenuItem_t*)0)) {
            const MenuItem_t* item = &page->items[s_tMenuState.cursor];//获取当前光标位置的菜单项指针

            if (item->type == MENU_ITEM_SUBPAGE) {
                menu_switch_page(item->target_page);
            }//如果菜单项类型是返回且当前页面有父页面，则切换到父页面
            else if (item->type == MENU_ITEM_BACK) {
                if (page->parent_id < PAGE_MAX) {//父页面 ID 小于 PAGE_MAX 表示有父页面
                    menu_switch_page(page->parent_id);
                }//如果菜单项类型是动作且有对应函数指针，则执行该函数
            }
            else if (item->type == MENU_ITEM_ACTION) {
                bool action_ok = true;

                if (item->action != (MenuActionFn)0) {
                    action_ok = item->action(item->bind_value);
                }

                if ((action_ok == true) && (item->target_page < PAGE_MAX)) {
                    menu_switch_page(item->target_page);
                }

                Menu_RequestRedraw();
            }
        }
        break;

        // 如果按下返回键且当前页面有父页面，则切换到父页面，最后根据需要标脏以触发重绘
    case MENU_KEY_BACK:
        if ((page->id == PAGE_RUNNING) || (page->id == PAGE_DEBUG_MENU)) {
            menu_return_home_to_idle_page();
        }
        else if (page->parent_id < PAGE_MAX) {
            menu_switch_page(page->parent_id);
            Menu_RequestRedraw();
        }
        //如果按下返回键但当前页面没有父页面，则不执行任何操作，保持在当前页面
        break;

        //对于其他按键或无效操作不执行任何操作，保持当前页面状态不变
    default:
        break;
    }
}

// 根据菜单状态渲染当前页面，只有在页面状态被标记为脏时才执行渲染操作，渲染完成后清除脏标志
void Menu_RenderIfDirty(void)
{
    //如果当前页面无效或状态未标脏则不执行任何渲染操作
    const MenuPage_t* page = Menu_GetCurrentPage();

    //如果页面无效或状态未标脏则不执行任何渲染操作
    if ((page == (const MenuPage_t*)0) || (s_tMenuState.dirty == false)) {
        return;
    }

    //渲染当前页面，优先使用页面自定义渲染函数，如果未提供则使用默认渲染函数，渲染前清屏以避免残影，渲染完成后清除脏标志
    (void)OLED_Clear();

    //如果页面提供了自定义渲染函数则调用该函数进行渲染，否则调用默认渲染函数，确保每个页面都能正确显示内容
    if (page->custom_render != (MenuRenderFn)0) {
        page->custom_render(page, &s_tMenuState);
    }
    else {
        menu_render_default_page(page, &s_tMenuState);
    }
    //渲染完成后清除脏标志，确保下次只有在状态变化时才会重新渲染，优化性能并减少 OLED 刷新次数
    s_tMenuState.dirty = false;
}

// 获取当前页面描述符，供外部查询当前页面信息使用，若当前页面 ID 无效则返回 NULL
void Menu_RequestRedraw(void)
{
    s_tMenuState.dirty = true;
}

// 判断当前页面状态是否被标记为脏，供外部查询是否需要刷新界面使用
bool Menu_IsDirty(void)
{
    return s_tMenuState.dirty;
}


// 获取当前页面描述符，供外部查询当前页面信息使用，若当前页面 ID 无效则返回 NULL
const MenuPage_t* Menu_GetCurrentPage(void)
{
    return menu_find_page(s_tMenuState.current_page);
}
