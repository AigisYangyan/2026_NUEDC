/**
 * @file    main.c
 * @brief   应用主入口（World-2 现役平台）
 *
 * 功能范围：
 * - 调用系统初始化 SysInit()（Board/Clock/Driver/Service + 开中断）
 * - 进入 World-2 主循环：每拍读 Clock_NowMs() 注入运行条目调度器 Scheduler_Run
 *
 * 运行模型（计划表 §22.0.3）：
 * 1. 空转态：Scheduler_Run 每拍只泵背景钩子 Menu_Tick（HMI：OLED + 按键，无事件不重渲染），
 *    别的后台全不开——极简省资源；
 * 2. 按键在菜单选中某运行条目后，该条目 on_step 每拍泵其作用域服务（如 SpeedTune 泵速度环）；
 * 3. 周期归各 Service 自门控（速度环 10ms = Chassis）。
 *
 * 旧 World-1 SysRun/task_scheduler 已停用（冻结不删，T01 删）。
 */

#include "app/scheduler/scheduler.h"
#include "app/system/sys_init.h"
#include "driver/clock/clock.h"

/* ---- 主入口 ------------------------------------------------------------- */

int main(void)
{
    SysInit();

    while (1) {
        /* 时间来源（Q1 定案）：装配层读 Clock_NowMs() 注入 scheduler，原值透传背景钩子与条目 step。 */
        Scheduler_Run(Clock_NowMs());
    }
}
