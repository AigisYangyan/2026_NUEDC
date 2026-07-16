/**
 * @file    menu_pages.c
 * @brief   OLED 菜单页面静态注册表实现
 *
 * 本文件负责集中定义页面描述符与菜单项静态表。
 *
 * 功能范围：
 * - 定义一级主菜单、DEBUG 二级菜单与统一运行页
 * - 根据运行项注册表自动构建菜单项数组
 * - 提供运行页统一渲染分发函数
 *
 * 设计约定：
 * - 页面注册采用编译期静态数组
 * - DEBUG 一级入口仍是普通子页面跳转，不属于运行项
 * - TASK1~4 与 DEBUG 子项统一从 run_registry.c 生成
 * - 新增一个运行项时，优先只在运行项注册表补一行
 */

#include <stdint.h>
#include "app/ui/oled/menu_pages.h"
#include "app/scheduler/run_registry.h"
#include "driver/oled/oled_hardware_i2c.h"

 /* ---- 布局常量 ----------------------------------------------------------- */

#define MENU_RUNNING_X          20u
#define MENU_RUNNING_Y          3u
#define MENU_HOME_FIXED_COUNT   1u

/* ---- 菜单静态缓存 ------------------------------------------------------- */

static MenuItem_t s_home_items[MENU_HOME_FIXED_COUNT + RUN_ENTRY_MAX_NUM];
static MenuItem_t s_debug_items[RUN_ENTRY_MAX_NUM];

/* ---- 静态渲染函数 ------------------------------------------------------- */

/**
 * @brief 统一运行页渲染函数
 * @note  若当前运行项提供自定义 render，则优先调用；否则显示 RUNNING
 */
static void menu_render_running_page(const MenuPage_t* page,
    const MenuState_t* state)
{
    const RunEntryReg_t* entry = Sys_GetActiveRunEntry();

    if ((entry != (const RunEntryReg_t*)0) &&
        (entry->custom_render != (MenuRenderFn)0)) {
        entry->custom_render(page, state);
        return;
    }

    ((void)(page));
    ((void)(state));
    (void)OLED_ShowString(MENU_RUNNING_X, MENU_RUNNING_Y, "RUNNING", 16u);
}

/* ---- 页面静态注册 ------------------------------------------------------- */

MenuPage_t g_menu_pages[] = {
    { PAGE_HOME,       "Menu",  PAGE_MAX,  s_home_items,  0u, (MenuRenderFn)0           },
    { PAGE_DEBUG_MENU, "DEBUG", PAGE_HOME, s_debug_items, 0u, (MenuRenderFn)0           },
    { PAGE_RUNNING,    "RUN",   PAGE_HOME, (const MenuItem_t*)0, 0u, menu_render_running_page }
};

const uint8_t g_menu_page_count =
(uint8_t)(sizeof(g_menu_pages) / sizeof(g_menu_pages[0]));

/* ---- 页面初始化接口 ----------------------------------------------------- */

void MenuPages_Init(void)
{
    uint8_t home_dynamic_count;
    uint8_t debug_item_count;

    s_home_items[0].text = "DEBUG";
    s_home_items[0].type = MENU_ITEM_SUBPAGE;
    s_home_items[0].target_page = PAGE_DEBUG_MENU;
    s_home_items[0].action = (MenuActionFn)0;
    s_home_items[0].bind_value = MENU_BIND_NONE;

    home_dynamic_count = RunRegistry_BuildMenuItems(RUN_MENU_HOME,
        &s_home_items[MENU_HOME_FIXED_COUNT],
        RUN_ENTRY_MAX_NUM);
    debug_item_count = RunRegistry_BuildMenuItems(RUN_MENU_DEBUG,
        s_debug_items,
        RUN_ENTRY_MAX_NUM);

    g_menu_pages[PAGE_HOME].item_count =
        (uint8_t)(MENU_HOME_FIXED_COUNT + home_dynamic_count);
    g_menu_pages[PAGE_DEBUG_MENU].item_count = debug_item_count;
}
