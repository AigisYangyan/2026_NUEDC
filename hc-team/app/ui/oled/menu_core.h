/**
 * @file    menu_core.h
 * @brief   轻量注册式 OLED 菜单核心对外接口
 *
 * 本模块定义菜单核心的数据结构与公开接口。
 *
 * 功能范围：
 * - 定义页面 ID、菜单项类型与菜单状态
 * - 声明菜单初始化、按键处理与渲染接口
 * - 为静态注册页面表提供统一的数据描述格式
 *
 * 设计约定：
 * - 菜单核心负责显示、导航与少量页面级状态切换链路
 * - 页面数据全部采用编译期静态注册，不使用动态内存
 */

#ifndef APP_UI_OLED_MENU_CORE_H
#define APP_UI_OLED_MENU_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/key/key.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* ---- 类型定义 ----------------------------------------------------------- */

#define MENU_BIND_NONE 0xFFFFu

    typedef enum {
        PAGE_HOME = 0,      // 一级主菜单
        PAGE_DEBUG_MENU,    // DEBUG 二级菜单
        PAGE_RUNNING,       // 统一运行页
        PAGE_MAX            // 页面总数上限
    } MenuPageId_e;

    typedef enum {
        MENU_ITEM_SUBPAGE = 0,//子页面跳转
        MENU_ITEM_ACTION,//执行函数
        MENU_ITEM_BACK//返回上一页
    } MenuItemType_e;

    typedef enum {
        MENU_KEY_NONE = 0,//无按键按下
        MENU_KEY_UP,//向上键
        MENU_KEY_DOWN,//向下键
        MENU_KEY_ENTER,//确认键
        MENU_KEY_BACK//返回键
    } MenuKeyAction_e;

    typedef bool (*MenuActionFn)(uint16_t bind_value);

    struct MenuPage;//前向声明，避免循环依赖
    struct MenuState;//前向声明，避免循环依赖

    /*
     * ============================================================================
     * 设计意图：
     *   实现"策略模式"，将不同页面的渲染逻辑抽象为统一接口，
     *   使菜单框架可以透明地调用任意页面的渲染函数，而无需关心具体实现细节。
     *
     * 核心作用：
     *   1. 多态分发：同一接口，不同页面有不同渲染实现
     *   2. 解耦框架：框架只依赖函数指针，不依赖具体页面代码
     *   3. 动态切换：运行时更换渲染函数，实现页面切换效果
     * ============================================================================
     */

    typedef void (*MenuRenderFn)(const struct MenuPage* page,
        const struct MenuState* state);


    // 菜单项描述符：文本 + 类型 + 目标页面/动作函数
    typedef struct {
        const char* text;
        MenuItemType_e type;//菜单项类型
        MenuPageId_e   target_page;//子页面跳转目标
        MenuActionFn   action;//执行函数指针
        uint16_t       bind_value;//动作绑定值，用于统一注册入口参数传递
    } MenuItem_t;

    // 页面描述符：ID + 标题 + 父页面 ID + 菜单项数组指针 + 菜单项数量 + 可选自定义渲染函数
    typedef struct MenuPage {
        MenuPageId_e      id;//页面 ID
        const char* title;//页面标题
        MenuPageId_e      parent_id;//父页面 ID，PAGE_MAX 表示无父页面
        const MenuItem_t* items;//页面包含的菜单项数组指针，若 item_count > 0 则不应为 NULL
        uint8_t           item_count;//页面包含的菜单项数量
        MenuRenderFn      custom_render;//页面自定义渲染函数指针，若为 NULL 则使用默认渲染
    } MenuPage_t;

    // 菜单状态：当前页面、光标位置、窗口顶部索引与脏标志
    typedef struct MenuState {
        MenuPageId_e current_page;//当前页面 ID
        uint8_t      cursor;//当前光标位置（相对于页面项数组）
        uint8_t      top_index;//当前窗口顶部的页面项索引（相对于页面项数组）
        bool    dirty;//页面内容是否被标脏需要刷新
    } MenuState_t;

    /* ---- 公开 API ----------------------------------------------------------- */

    /**
     * @brief 菜单初始化
     */
    void Menu_Init(void);

    /**
     * @brief 处理菜单按键输入
     * @param key 物理按键 ID
     */
    void Menu_HandleKey(Key_Id_e key);

    /**
     * @brief 按需渲染当前页面
     */
    void Menu_RenderIfDirty(void);

    /**
     * @brief 切换当前页面
     * @param page_id 目标页面 ID
     */
    void Menu_SetCurrentPage(MenuPageId_e page_id);

    /**
     * @brief 请求下一次刷新
     */
    void Menu_RequestRedraw(void);

    /**
     * @brief 查询当前页面是否需要刷新
     * @return true 表示存在待刷新的页面内容
     */
    bool Menu_IsDirty(void);

    /**
     * @brief  获取当前页面描述符
     * @return 当前页面指针
     */
    const MenuPage_t* Menu_GetCurrentPage(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_OLED_MENU_CORE_H */
