/**
 * @file    run_registry.h
 * @brief   OLED 运行项统一注册窗口对外接口
 *
 * 本头文件负责导出运行项静态注册表与查找/建表接口。
 *
 * 功能范围：
 * - 导出统一运行项注册表
 * - 提供运行项 ID 查找接口
 * - 提供菜单项自动构建接口
 *
 * 设计约定：
 * - 所有 TASK 与 DEBUG 子项统一在一张静态表中注册
 * - 菜单层只消费注册结果，不再手写多组进入回调
 * - 不使用动态内存，全部采用编译期静态数组
 */

#ifndef APP_SCHEDULER_RUN_REGISTRY_H
#define APP_SCHEDULER_RUN_REGISTRY_H

#include "app/scheduler/task_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 运行项注册表导出 --------------------------------------------------- */

extern const RunEntryReg_t g_run_entries[];
extern const uint8_t       g_run_entry_count;

/* ---- 运行项接口 --------------------------------------------------------- */

const RunEntryReg_t* RunRegistry_FindById(RunEntryId_e id);
uint8_t RunRegistry_BuildMenuItems(RunEntryMenuGroup_e group,
                                   MenuItem_t* out_items,
                                   uint8_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_RUN_REGISTRY_H */
