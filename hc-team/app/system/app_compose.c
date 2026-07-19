/**
 * @file    app_compose.c
 * @brief   World-2 运行栈装配：运行条目表 + 菜单分组表 + 生命周期钩子适配。
 *
 * 交互模型（计划表 §22.0.3）：
 * - 空转态：scheduler 每拍只泵背景钩子 Menu_Tick（HMI：OLED 刷新 + 按键中断事件，
 *   无事件不重渲染），别的后台全不开；
 * - 按键在菜单选中某运行条目 → Scheduler_EnterEntry → on_enter 注册作用域数据组、
 *   on_step 每拍泵作用域服务、BACK → on_exit 停；
 * - 周期归各 Service 自门控（单一所有者：速度环 10ms = Chassis 内部 Clock 门控）。
 *
 * 首个运行条目 SpeedTune = 底盘速度环 VOFA 调参：三钩子直接派发 tuning 服务
 * （进页即注册 VOFA 表、10ms 泵速度环 + 发帧、退页刹停清表）。
 * 换/加 debug/test 项：在 s_entries[] 补条目 + 在对应分组的 entries 数组补其下标。
 */
#include "app/system/app_compose.h"

#include <stddef.h>
#include <stdint.h>

#include "app/scheduler/scheduler.h"
#include "app/service/tuning/tuning.h"
#include "app/ui/menu/menu.h"

/* ---- SpeedTune 运行条目钩子（适配 Scheduler_Entry_T 签名 → tuning 服务）------ */

static void speedtune_enter(void)
{
    /* 进页即注册 VOFA 速度环调参组 + 确定性安全停（tuning 内部清表/排空/置安全 cmd）。 */
    (void)Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED);
}

static void speedtune_step(uint32_t now_ms)
{
    (void)now_ms;   /* tuning 是 Service，周期由其自身 Clock 门控（10ms 单一所有者），不用注入时刻 */
    Tuning_Update();
}

static void speedtune_exit(void)
{
    Tuning_ExitProfile();   /* 退页：Chassis_Stop 刹停 + 清 VOFA 表 */
}

/* ---- 运行条目表（scheduler 全局条目索引 = 本数组下标）----------------------- */

static const Scheduler_Entry_T s_entries[] = {
    { "SpeedTune", speedtune_enter, speedtune_step, speedtune_exit },  /* idx 0 */
};

/* ---- 菜单分组表（DEBUG 运行分类的条目 = 上表下标）--------------------------- */

static const uint8_t s_debug_entries[] = { 0u };  /* → s_entries[0] SpeedTune */

static const Menu_Group_T s_groups[] = {
    { "DEBUG", MENU_GROUP_RUN, s_debug_entries,
      (uint8_t)(sizeof(s_debug_entries) / sizeof(s_debug_entries[0])),
      NULL, 0u },
};

/* ---- 装配入口 ----------------------------------------------------------- */

void AppCompose_Install(void)
{
    Scheduler_Init(s_entries,
                   (uint8_t)(sizeof(s_entries) / sizeof(s_entries[0])),
                   Menu_Tick);
    Menu_Setup(s_groups,
               (uint8_t)(sizeof(s_groups) / sizeof(s_groups[0])));
}
