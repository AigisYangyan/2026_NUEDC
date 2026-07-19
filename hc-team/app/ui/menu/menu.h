/**
 * @file    menu.h
 * @brief   板载菜单（App UI 层）——两级分类导航 + 参数表的唯一上层导航面。
 *
 * 抽象（菜单能做什么）：
 * - 列出并选择一级分类（DEBUG/TEST/PARAMS/TASK…，名字由装配层给），下探到其二级子菜单；
 * - 在运行分类里列出并选择运行条目（分问选择），启动/停止一个条目；
 * - 在参数分类里列出并就地调整一组命名参数（按钮动态调参，供封箱现场无上位机连接时用）；
 * - 报告当前所处界面；
 * - 被周期泵送以消费语义输入、驱动条目切换、按需渲染。
 *
 * 隐藏：
 * - 用了哪些 Service（hmi 面板 / scheduler 条目表）、导航状态机、光标/滚动窗口、
 *   参数编辑焦点、渲染布局、int32→dec 格式化细节、分类→scheduler 全局索引的映射。
 *
 * 分层与所有权（AGENTS.md §4，UI 属 Task/Scheduler/UI 层）：
 * - 运行条目枚举与 enter/exit 转移序唯一在 scheduler（菜单只调 Enter/Leave/查询，不复算转移；
 *   RUN 分类的 entries[] 是 scheduler 全局条目索引，菜单只是其上的视图）；
 * - 语义输入映射与行式显示唯一在 hmi（菜单只调 PollInput/Update/PrintLine/IsDisplayReady）；
 * - 参数值存储与限幅唯一在调用者 accessor 背后的拥有 Service（菜单零值副本、零限幅）。
 *   本模块唯一拥有：两级导航状态机 + 光标/滚动窗口 + 参数编辑焦点 + int32→dec 显示格式化。
 *
 * 调参双通道隔离（用户裁定 2026-07-18）：PARAM 分类 = 按钮动态调参（本菜单）；VOFA 静态调参走
 * 独立平行链（tuning/S03），两者当前互不联通——菜单不接 VOFA、不复做限幅（单一所有者）。
 *
 * 调用前置条件（由 SYS01/T01 装配层负责，非本模块）：
 * - Menu_Setup 前已完成 Scheduler_Init（条目表登记）与 Hmi_Init；
 * - 主循环把 Menu_Tick 注册为 Scheduler 背景钩子（或直接周期调用）；
 * - 分组表（含各组的 entries 索引数组 / 参数表）由装配层提供，其参数 accessor 内部调各拥有
 *   Service 的公共 API。UI01 只交付构件，零调用者是预期状态。
 *
 * 显示为 ASCII（hmi 16 列 × 8px 字宽，无中文字模）：分类名、条目名、参数名与标题均须 ASCII。
 */
#ifndef APP_UI_MENU_MENU_H
#define APP_UI_MENU_MENU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 菜单所处界面（两级：GROUP_LIST 为 L1 根界面，RUN_LIST/PARAM_LIST 为 L2）。 */
typedef enum {
    MENU_SCREEN_GROUP_LIST = 0, /* L1 根界面：一级分类列表 */
    MENU_SCREEN_RUN_LIST,       /* L2：某运行分类的条目子列表 */
    MENU_SCREEN_RUN_ACTIVE,     /* 某条目激活中：菜单画统一 RUNNING 横幅，仅响应 BACK 停止 */
    MENU_SCREEN_PARAM_LIST,     /* L2：某参数分类的参数表浏览 */
    MENU_SCREEN_PARAM_EDIT,     /* 单参数就地调整 */
} Menu_Screen;

/**
 * @brief 可调参数描述符（值存储/限幅归拥有 Service，菜单只经 get/set 读写）。
 * @note  name 不得为 NULL。两类项：
 *        - 普通参数项（action==NULL）：name/get/set 不得为 NULL；PARAM_LIST 上 K3 进 EDIT，
 *          UP/DOWN=set(get()±step)。单位、精度、限幅由参数拥有者定，菜单不复做。
 *        - 动作项（action!=NULL）：PARAM_LIST 上 K3 直接调 action() 并停留列表（不进 EDIT）；
 *          get/set/step 忽略（可为 NULL/0）。用于 SAVE/RESET/APPLY 一类一次性命令按钮。
 */
typedef struct {
    const char *name;            /* ASCII 参数名（显示用），不得为 NULL */
    int32_t   (*get)(void);      /* 读当前值（整数口径）；动作项可为 NULL */
    void      (*set)(int32_t v); /* 写新值（经拥有它的 Service API 应用；限幅归拥有者）；动作项可为 NULL */
    int32_t     step;            /* 每次 UP/DOWN 的调整增量；动作项忽略 */
    void      (*action)(void);   /* 非 NULL = 动作项：K3 调它并停留 PARAM_LIST，不进 EDIT */
} Menu_Param_T;

/** 一级分类的种类：运行条目组 或 参数组（互斥）。 */
typedef enum {
    MENU_GROUP_RUN = 0,  /* 运行条目组：选中→条目子列表→Scheduler_EnterEntry */
    MENU_GROUP_PARAM,    /* 参数组（按钮动态调参）：选中→参数表 */
} Menu_GroupKind;

/**
 * @brief 一级分类描述符（由装配层命名与填充；菜单只是其上的视图/导航）。
 * @note  name 不得为 NULL。kind==MENU_GROUP_RUN 时用 entries/entry_count（entries 为
 *        scheduler 全局条目索引数组），params 忽略；kind==MENU_GROUP_PARAM 时用
 *        params/param_count，entries 忽略。entry_count/param_count 为 0 均合法（空子列表）。
 */
typedef struct {
    const char        *name;         /* ASCII 分类名（显示用），不得为 NULL */
    Menu_GroupKind      kind;
    const uint8_t      *entries;     /* kind==RUN：scheduler 全局条目索引数组 */
    uint8_t             entry_count; /* kind==RUN：条目数 */
    const Menu_Param_T *params;      /* kind==PARAM：参数表 */
    uint8_t             param_count; /* kind==PARAM：参数个数 */
} Menu_Group_T;

/**
 * @brief 复位导航状态为 GROUP_LIST + 待渲染；不触碰任何硬件/Service。
 * @param groups       分组表；调用方保证其（连同各组 entries/params 表）生命周期覆盖使用期。
 *                     NULL（配合 group_count=0）合法——空菜单。
 * @param group_count  一级分类个数。
 * @note  名为 Setup 而非 Init：冻结旧 menu_core.c 仍导出 Menu_Init，双实现共链期符号冲突
 *        （契约修订 1）。Menu_Tick/Menu_GetScreen 与旧符号无冲突，保持原名。
 */
void Menu_Setup(const Menu_Group_T *groups, uint8_t group_count);

/**
 * @brief 周期泵送（匹配 Scheduler background_step 签名）。每拍：
 *        ① Hmi_Update()（面板泵送，hmi 自门控 5ms）；
 *        ② Hmi_PollInput() 取一个语义事件 → 依当前界面转移/编辑/切换 scheduler 条目；
 *        ③ 有待渲染且 Hmi_IsDisplayReady() → 经 Hmi_PrintLine 渲染当前界面（含 RUN_ACTIVE 的 RUNNING 横幅）。
 * @param now_ms 预留以匹配钩子签名；当前菜单事件驱动、不做时间门控（门控归 hmi/scheduler）。
 * @note  RUN_ACTIVE 期菜单只写固定 RUNNING 横幅（row0）+ 清 row1..3；条目自绘整屏＝未来按条目
 *        opt-in flag，当前无条目 opt-in（§23.0 修订 UI01 显示所有权契约）。
 */
void Menu_Tick(uint32_t now_ms);

/** 当前界面（查询/渲染所需）。 */
Menu_Screen Menu_GetScreen(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_MENU_MENU_H */
