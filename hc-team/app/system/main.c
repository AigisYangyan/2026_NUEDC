/**
 * @file    main.c
 * @brief   应用主入口
 *
 * 本文件提供 MCU 应用程序的最上层入口。
 *
 * 功能范围：
 * - 调用系统初始化与主运行入口
 * - 作为应用层启动链路的统一落点
 *
 * 实现说明：
 * 1. `SysInit()` 内调用 `SYSCFG_DL_init()` 完成芯片外设基础配置
 * 2. 再完成工程内模块初始化
 * 3. 最后进入 `SysRun()`，由系统调度器接管后续运行
 */

#include "app/scheduler/task_scheduler.h"

/* ---- 主入口 ------------------------------------------------------------- */

int main(void)
{
    SysInit();
    SysRun();

    while (1) {
    }
}
