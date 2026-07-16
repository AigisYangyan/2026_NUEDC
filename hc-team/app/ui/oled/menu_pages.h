/**
 * @file    menu_pages.h
 * @brief   菜单页面静态注册表声明
 *
 * 本头文件负责导出菜单页面静态表、页面数量以及页面初始化接口。
 *
 * 功能范围：
 * - 导出页面描述符静态数组
 * - 导出页面总数
 * - 提供页面静态缓存初始化入口
 *
 * 不负责的内容：
 * - 菜单按键处理和页面跳转
 * - 页面渲染具体实现
 * - 运行项注册逻辑
 */

#ifndef APP_UI_OLED_MENU_PAGES_H
#define APP_UI_OLED_MENU_PAGES_H

#include "app/ui/oled/menu_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 页面注册表导出 ----------------------------------------------------- */

extern MenuPage_t g_menu_pages[];
extern const uint8_t g_menu_page_count;

/* ---- 页面初始化接口 ----------------------------------------------------- */

void MenuPages_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_OLED_MENU_PAGES_H */
