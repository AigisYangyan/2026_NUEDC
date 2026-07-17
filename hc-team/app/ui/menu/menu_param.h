/**
 * @file    menu_param.h
 * @brief   参数表子视图（app/ui/menu 私有子模块）——PARAM_LIST/PARAM_EDIT 两界面。
 *
 * 职责：持有调用者参数表指针（不拷贝值），拥有参数界面的光标/滚动窗口/编辑焦点，
 * 经 hmi 渲染，经 accessor 就地读写。值存储与限幅归拥有 Service（本子模块零复做）。
 *
 * 本头仅供同目录 menu.c 引用；不进入对上层的公共面。
 */
#ifndef APP_UI_MENU_MENU_PARAM_H
#define APP_UI_MENU_MENU_PARAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/service/hmi/hmi.h" /* Hmi_Input（UI→Service，合法） */
#include "app/ui/menu/menu.h"    /* Menu_Param_T / Menu_Screen */

#ifdef __cplusplus
extern "C" {
#endif

/** 登记参数表并复位光标/窗口/编辑焦点为列表顶、非编辑。 */
void MenuParam_Init(const Menu_Param_T *params, uint8_t count);

/** 进入参数界面：复位到列表顶、非编辑（供 menu.c 从 RUN_LIST 切入时调用）。 */
void MenuParam_Enter(void);

/**
 * @brief 处理一个语义输入事件，返回处理后应处于的界面。
 * @param ev  语义输入（透传自 hmi）。
 * @return MENU_SCREEN_PARAM_LIST / MENU_SCREEN_PARAM_EDIT 表示仍在参数界面；
 *         MENU_SCREEN_RUN_LIST 表示请求退回运行选择界面。
 */
Menu_Screen MenuParam_Handle(Hmi_Input ev);

/** 依内部界面态经 Hmi_PrintLine 渲染当前参数子界面（列表或编辑）。 */
void MenuParam_Render(void);

/**
 * @brief 把 int32 参数值格式化为十进制显示串（含负号）。
 * @param value  待格式化的值。
 * @param buf    输出缓冲。
 * @param cap    缓冲容量；须 ≥ 12（-2147483648 + NUL）方能容纳全范围。
 * @note  编辑界面的值行由此产出；独立可测（边界：INT32_MIN/0/负值/多位）。
 */
void MenuParam_FormatValue(int32_t value, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_MENU_MENU_PARAM_H */
