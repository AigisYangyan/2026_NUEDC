/**
 * @file    run_registry.c
 * @brief   OLED 运行项统一注册窗口实现
 *
 * 本文件集中维护“菜单项 - 运行状态 - 任务组 - 显示行为”的单源注册表。
 *
 * 功能范围：
 * - 统一登记 TASK1~4 与 DEBUG 子项
 * - 提供运行项查找接口
 * - 按菜单分组自动生成 MenuItem_t 数组
 *
 * 设计约定：
 * - 新增一个运行项时，优先只在本文件补一行注册
 * - 若该项后台任务尚未接好，可将 task_group 置空，系统自动退化为 UI 占位
 * - 进入运行项统一走 Sys_EnterRunEntry()，避免菜单层重复写进入回调
 */

#include <stdint.h>
#include <stdbool.h>
#include "app/scheduler/run_registry.h"
#include "app/tasks/task_groups.h"
#include "app/tasks/speed_loop/speed_loop.h"
#include "app/tasks/gray_test/gray_test.h"
#include "app/tasks/uart_test/uart_test.h"
#include "app/tasks/uart_stress/uart_stress.h"
#include "app/tasks/platform_2d/2DPlatform_LaserStrike.h"
#include "app/tasks/task1/task1.h"

/* ---- 静态动作函数 ------------------------------------------------------- */

static bool run_registry_enter_action(uint16_t bind_value)
{
  return Sys_EnterRunEntry((RunEntryId_e)bind_value);
}

/* ---- 统一运行项注册表 --------------------------------------------------- */
/*
 * 说明：
 * 1. 本表是“菜单入口 -> 运行页 -> 任务组 -> 行为钩子”的唯一绑定点；
 * 2. TASK1~4 与 DEBUG 子项统一在这里注册，不再分别维护动作回调表；
 * 3. 若某项后台任务尚未接好，可将 task_group 置空，系统自动回落到 UI 占位。
 *
 * 当前首个实装运行项是 RUN_ENTRY_DEBUG_SPEED：
 * - 进入后切到 PAGE_RUNNING；
 * - 调度切到 g_tSpeedLoopTaskGroup；
 * - 进入/退出分别调用 SpeedLoop_Enter()/SpeedLoop_Exit()；
 * - 该项当前是首个真正挂接专属后台任务组的 DEBUG 运行项。
 */

const RunEntryReg_t g_run_entries[] = {
  // id                      name       menu_group     target_page   task_group    render_fn       on_enter       on_exit
  // --- TASK1~4 是预留的通用任务入口，当前未挂接专属任务组，进入后回落到 UI 占位
{ RUN_ENTRY_TASK1,         "TASK1",   RUN_MENU_HOME,  PAGE_RUNNING, &g_tTask1TaskGroup,
  (MenuRenderFn)0, Task1_Enter, Task1_Exit },
{ RUN_ENTRY_TASK2,         "TASK2",   RUN_MENU_HOME,  PAGE_RUNNING, (const TaskGroup_T*)0,
  (MenuRenderFn)0, (RunEntryHookFn)0, (RunEntryHookFn)0 },
{ RUN_ENTRY_TASK3,         "TASK3",   RUN_MENU_HOME,  PAGE_RUNNING, (const TaskGroup_T*)0,
  (MenuRenderFn)0, (RunEntryHookFn)0, (RunEntryHookFn)0 },
{ RUN_ENTRY_TASK4,         "TASK4",   RUN_MENU_HOME,  PAGE_RUNNING, (const TaskGroup_T*)0,
  (MenuRenderFn)0, (RunEntryHookFn)0, (RunEntryHookFn)0 },

  // --- DEBUG 子项，进入后切到 PAGE_RUNNING，部分项已挂接专属任务组和行为钩子
  //速度环测试
{ RUN_ENTRY_DEBUG_SPEED,   "Speed",   RUN_MENU_DEBUG, PAGE_RUNNING, &g_tSpeedLoopTaskGroup,
  (MenuRenderFn)0, SpeedLoop_Enter, SpeedLoop_Exit },

  //串口收发测试(是否丢包,丢帧)
{ RUN_ENTRY_DEBUG_UART_TEST, "UART_TEST", RUN_MENU_DEBUG, PAGE_RUNNING, &g_tUartTestTaskGroup,
  (MenuRenderFn)0, UartTest_Enter, UartTest_Exit },

  //UART 230400 5ms 压测：主发 8B + echo，PB22 LED 100ms 闪烁指示运行态
{ RUN_ENTRY_DEBUG_UART_STRESS, "UART_Stress", RUN_MENU_DEBUG, PAGE_RUNNING, &g_tUartStressTaskGroup,
  (MenuRenderFn)0, UartStress_Enter, UartStress_Exit },

  //灰度测试,是否有数据振动
{ RUN_ENTRY_DEBUG_GRAY_TEST, "GRAY_TEST", RUN_MENU_DEBUG, PAGE_RUNNING, &g_tGrayTestTaskGroup,
  (MenuRenderFn)0, GrayTest_Enter, GrayTest_Exit },

  //视觉数据流测试（backend: DEBUG_Vision_data）
{ RUN_ENTRY_DEBUG_VISION_DATA, "Vision_data", RUN_MENU_DEBUG, PAGE_RUNNING,
  &g_tDebugVisionDataTaskGroup,
  (MenuRenderFn)0, DebugVisionData_Enter, DebugVisionData_Exit },

  //循迹测试, 直接输出循迹误差数值, 观察数值稳定性和丢线回退表现
{ RUN_ENTRY_DEBUG_TRACK,   "Track",   RUN_MENU_DEBUG, PAGE_RUNNING, (const TaskGroup_T*)0,
  (MenuRenderFn)0, (RunEntryHookFn)0, (RunEntryHookFn)0 },

  //步进电机单轴 PID 调参测试
{ RUN_ENTRY_DEBUG_STEPPER_X, "Stepper_X", RUN_MENU_DEBUG, PAGE_RUNNING, &g_tVisionTrackTaskGroup,
  (MenuRenderFn)0, StepperTestX_Enter, StepperTestX_Exit },
{ RUN_ENTRY_DEBUG_STEPPER_Y, "Stepper_Y", RUN_MENU_DEBUG, PAGE_RUNNING, &g_tVisionTrackTaskGroup,
  (MenuRenderFn)0, StepperTestY_Enter, StepperTestY_Exit },

  //单轴 EMM42 机械平顺性 / 固件 PID 调参测试
{ RUN_ENTRY_DEBUG_MOTOR_SMOOTHNESS_SPEEDMODE_VELACC, "DEBUG_Smooth", RUN_MENU_DEBUG,
  PAGE_RUNNING, &g_tDebugSmoothTaskGroup,
  (MenuRenderFn)0, DebugSmooth_Enter, DebugSmooth_Exit }
};

const uint8_t g_run_entry_count =
(uint8_t)(sizeof(g_run_entries) / sizeof(g_run_entries[0]));

/* ---- 对外接口实现 ------------------------------------------------------- */

// 通过 ID 查找运行项注册信息
const RunEntryReg_t* RunRegistry_FindById(RunEntryId_e id)
{
  for (uint8_t i = 0u; i < g_run_entry_count; i++) {
    if (g_run_entries[i].id == id) {
      return &g_run_entries[i];
    }//找到匹配项，返回指向该项的指针
  }

  return (const RunEntryReg_t*)0;//未找到匹配项，返回空指针
}

// 构建指定菜单分组的 MenuItem_t 数组，返回实际填充的项数
uint8_t RunRegistry_BuildMenuItems(RunEntryMenuGroup_e group,
  MenuItem_t* out_items,//输出数组指针
  uint8_t max_count)//输出数组最大容量
{
  uint8_t item_count = 0u;//实际填充的项数

  if (out_items == (MenuItem_t*)0) {
    return 0u;//输出数组指针无效，返回 0
  }

  for (uint8_t i = 0u; i < g_run_entry_count; i++) {
    const RunEntryReg_t* entry = &g_run_entries[i];//当前项指针

    if ((entry->menu_group != group) || (item_count >= max_count)) {
      continue;//跳过不匹配的项或达到最大数量限制
    }

    out_items[item_count].text = entry->name;//设置菜单项文本
    out_items[item_count].type = MENU_ITEM_ACTION;//设置菜单项类型
    out_items[item_count].target_page = entry->target_page;//设置目标页面
    out_items[item_count].action = run_registry_enter_action;//设置动作函数
    out_items[item_count].bind_value = (uint16_t)entry->id;//设置绑定值
    item_count++;//增加实际填充的项数
  }

  return item_count;//返回实际填充的项数
}
