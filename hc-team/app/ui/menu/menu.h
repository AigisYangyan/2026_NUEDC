/**
 * @file    menu.h
 * @brief   板载菜单（App UI 层）——分问选择 + 参数表的唯一上层导航面。
 *
 * 抽象（菜单能做什么）：
 * - 列出并选择运行条目（分问选择），启动/停止一个条目；
 * - 列出并就地调整一组命名参数（参数表，供封箱现场无上位机连接时调参）；
 * - 报告当前所处界面；
 * - 被周期泵送以消费语义输入、驱动条目切换、按需渲染。
 *
 * 隐藏：
 * - 用了哪些 Service（hmi 面板 / scheduler 条目表）、导航状态机、光标/滚动窗口、
 *   参数编辑焦点、渲染布局、int32→dec 格式化细节。
 *
 * 分层与所有权（AGENTS.md §4，UI 属 Task/Scheduler/UI 层）：
 * - 运行条目枚举与 enter/exit 转移序唯一在 scheduler（菜单只调 Enter/Leave/查询，不复算转移）；
 * - 语义输入映射与行式显示唯一在 hmi（菜单只调 PollInput/Update/PrintLine/IsDisplayReady）；
 * - 参数值存储与限幅唯一在调用者 accessor 背后的拥有 Service（菜单零值副本、零限幅）。
 *   本模块唯一拥有：导航界面状态机 + 光标/滚动窗口 + 参数编辑焦点 + int32→dec 显示格式化。
 *
 * 调用前置条件（由 SYS01/T01 装配层负责，非本模块）：
 * - Menu_Setup 前已完成 Scheduler_Init（条目表登记）与 Hmi_Init；
 * - 主循环把 Menu_Tick 注册为 Scheduler 背景钩子（或直接周期调用）；
 * - 参数表由装配层提供，其 accessor 内部调各拥有 Service 的公共 API。
 *
 * 显示为 ASCII（hmi 16 列 × 8px 字宽，无中文字模）：条目名、参数名与标题均须 ASCII；
 * 参数尾项以 "Params" 呈现。
 */
#ifndef APP_UI_MENU_MENU_H
#define APP_UI_MENU_MENU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 菜单所处界面。 */
typedef enum {
    MENU_SCREEN_RUN_LIST = 0,   /* 分问选择：scheduler 条目 +（有参数时）Params 尾项 */
    MENU_SCREEN_RUN_ACTIVE,     /* 某条目激活中：菜单让出整屏显示，仅响应 BACK 停止 */
    MENU_SCREEN_PARAM_LIST,     /* 参数表浏览 */
    MENU_SCREEN_PARAM_EDIT,     /* 单参数就地调整 */
} Menu_Screen;

/**
 * @brief 可调参数描述符（值存储/限幅归拥有 Service，菜单只经 get/set 读写）。
 * @note  name/get/set 不得为 NULL；单位、精度、限幅由参数拥有者定，菜单不复做。
 */
typedef struct {
    const char *name;            /* ASCII 参数名（显示用），不得为 NULL */
    int32_t   (*get)(void);      /* 读当前值（整数口径） */
    void      (*set)(int32_t v); /* 写新值（经拥有它的 Service API 应用；限幅归拥有者） */
    int32_t     step;            /* 每次 UP/DOWN 的调整增量 */
} Menu_Param_T;

/**
 * @brief 复位导航状态为 RUN_LIST + 待渲染；不触碰任何硬件/Service。
 * @param params       参数表；调用方保证其生命周期覆盖使用期。
 *                     NULL（配合 param_count=0）合法——无 Params 尾项。
 * @param param_count  参数个数。
 * @note  名为 Setup 而非 Init：冻结旧 menu_core.c 仍导出 Menu_Init，双实现共链期符号冲突
 *        （契约修订 1）。Menu_Tick/Menu_GetScreen 与旧符号无冲突，保持原名。
 */
void Menu_Setup(const Menu_Param_T *params, uint8_t param_count);

/**
 * @brief 周期泵送（匹配 Scheduler background_step 签名）。每拍：
 *        ① Hmi_Update()（面板泵送，hmi 自门控 5ms）；
 *        ② Hmi_PollInput() 取一个语义事件 → 依当前界面转移/编辑/切换 scheduler 条目；
 *        ③ 非 RUN_ACTIVE 且有待渲染且 Hmi_IsDisplayReady() → 经 Hmi_PrintLine 渲染当前界面。
 * @param now_ms 预留以匹配钩子签名；当前菜单事件驱动、不做时间门控（门控归 hmi/scheduler）。
 * @note  RUN_ACTIVE 期菜单不写任何显示行——整屏显示所有权归激活条目的 on_step。
 */
void Menu_Tick(uint32_t now_ms);

/** 当前界面（查询/渲染所需）。 */
Menu_Screen Menu_GetScreen(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_MENU_MENU_H */
