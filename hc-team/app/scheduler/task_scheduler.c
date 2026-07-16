/**
 * @file    task_scheduler.c
 * @brief   系统主调度实现
 *
 * 本文件负责运行态切换与任务组调度。
 *
 * 功能范围：
 * - 维护系统状态机当前状态
 * - 在主循环中调度当前状态下登记的任务组
 * - 在 1ms 时间基中推进当前状态对应任务的时间片
 *
 * 实现说明：
 * 1. INIT 状态只负责一次性初始化，不注册周期任务
 * 2. 进入运行流程后切到 IDLE_PAGE，由当前运行项决定后台实际执行内容
 * 3. 调度器本身不关心具体业务，只按 Enable / Run 标志执行登记任务
 */

#include <stdint.h>
#include <stdbool.h>
#include "app/scheduler/task_scheduler.h"
#include "app/scheduler/run_registry.h"
#include "app/tasks/task_groups.h"
#include "driver/clock/clock.h"

/* ---- 系统状态变量 ------------------------------------------------------- */

SYS_FLAG_TASK_E g_eSysFlagManage;
static const RunEntryReg_t* s_pActiveRunEntry = (const RunEntryReg_t*)0;
static uint32_t s_last_tick_ms = 0u;

/* ---- 静态函数声明 ------------------------------------------------------- */

static void TaskStartSchedule(void); //系统主调度入口，执行当前状态下登记的任务组
static const TaskGroup_T* SysResolveCurrentTaskGroup(void);//解析当前系统应使用的任务组
static void SysRunEntryHook(void); //运行入口空钩子，预留给后续模块单独挂接初始化

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 系统主运行入口
 * @note  完成运行态必要初始化后切到 IDLE_PAGE，并进入任务调度主循环
 */
void SysRun(void)
{
    SysRunEntryHook();
    Sys_LeaveRunEntry();
    TaskStartSchedule();//分发开始
}

const RunEntryReg_t* Sys_GetActiveRunEntry(void)
{
    return s_pActiveRunEntry;
}

bool Sys_EnterRunEntry(RunEntryId_e id)
{
    const RunEntryReg_t* entry = RunRegistry_FindById(id);

    if (entry == (const RunEntryReg_t*)0) {
        return false;
    }

    if ((s_pActiveRunEntry != (const RunEntryReg_t*)0) &&
        (s_pActiveRunEntry->on_exit != (RunEntryHookFn)0)) {
        s_pActiveRunEntry->on_exit();
    }

    s_pActiveRunEntry = entry;
    g_eSysFlagManage = SYS_STA_RUNNING;

    if (entry->on_enter != (RunEntryHookFn)0) {
        entry->on_enter();
    }

    return true;
}

void Sys_LeaveRunEntry(void)
{
    if ((s_pActiveRunEntry != (const RunEntryReg_t*)0) &&
        (s_pActiveRunEntry->on_exit != (RunEntryHookFn)0)) {
        s_pActiveRunEntry->on_exit();
    }

    s_pActiveRunEntry = (const RunEntryReg_t*)0;
    g_eSysFlagManage = SYS_STA_IDLE_PAGE;
}

/**
 * @brief 当前状态任务调度主循环 -- 相当于闹钟
 * @note  只执行当前 SYS 状态下已登记且 Run 置位的任务
 */
static void TaskStartSchedule(void)
{
    const TaskGroup_T* p_task_group;//当前状态对应的任务组指针
    uint32_t now_ms;
    uint32_t elapsed_ms;

    s_last_tick_ms = Clock_NowMs();

    while (1) {
        /* 主动拉取 elapsed 毫秒并推进时间片；uint32_t 无符号减法天然处理回绕。 */
        now_ms = Clock_NowMs();
        elapsed_ms = now_ms - s_last_tick_ms;
        s_last_tick_ms = now_ms;

        while (elapsed_ms > 0u) {
            TaskTimeSliceManage();
            elapsed_ms--;
        }

        /* 仅调度当前 SYS 状态下登记的任务组。 */
        p_task_group = SysResolveCurrentTaskGroup();
        if ((p_task_group == (const TaskGroup_T*)0) ||
            (p_task_group->pTaskList == (TaskComps_T*)0)) {
            continue;
        }//检验组是否存在并且组内任务表是否存在

        for (uint8_t i = 0; i < p_task_group->TaskCount; i++) {
            if ((p_task_group->pTaskList[i].Enable == TASK_ENABLE) &&
                (p_task_group->pTaskList[i].Run != 0u)) {
                p_task_group->pTaskList[i].pTaskFunc();
                p_task_group->pTaskList[i].Run = 0u;
            }
        }
    }
}

/**
 * @brief 任务时间片推进函数 --- 对任务进行时间管理, 相当于时钟本身
 * @note  需在 1ms 周期中断中调用，用于递减当前状态任务计数器并置位 Run 标志
 */
void TaskTimeSliceManage(void)
{
    const TaskGroup_T* p_task_group = SysResolveCurrentTaskGroup();

    /* 仅推进当前 SYS 状态下登记任务的时间片。 */
    if ((p_task_group == (const TaskGroup_T*)0) || (p_task_group->pTaskList == (TaskComps_T*)0)) {
        return;
    }//检验组是否存在并且组内任务表是否存在

    for (uint8_t i = 0; i < p_task_group->TaskCount; i++) {
        if ((p_task_group->pTaskList[i].Enable == TASK_ENABLE) &&
            (p_task_group->pTaskList[i].TimCount != 0u)) {
            p_task_group->pTaskList[i].TimCount--;
            if (p_task_group->pTaskList[i].TimCount == 0u) {
                p_task_group->pTaskList[i].TimCount = p_task_group->pTaskList[i].TimRload;
                p_task_group->pTaskList[i].Run = 1u;
            }
        }//如果任务启用且计数器不为0，递减计数器；如果递减后计数器为0，重装计数器并置位 Run 标志
    }
}

/**
 * @brief  获取当前系统状态对应的任务组
 * @return 当前任务组指针；若状态非法则返回空指针
 */
static const TaskGroup_T* SysResolveCurrentTaskGroup(void)
{
    if ((uint8_t)g_eSysFlagManage >= (uint8_t)SYS_STA_MAX_NUM) {
        return (const TaskGroup_T*)0;
    }

    switch (g_eSysFlagManage) {
    case SYS_STA_INIT:
        return (const TaskGroup_T*)0;

    case SYS_STA_IDLE_PAGE:
        return &g_tUiTaskGroup;

    case SYS_STA_RUNNING:
        if ((s_pActiveRunEntry != (const RunEntryReg_t*)0) &&
            (s_pActiveRunEntry->task_group != (const TaskGroup_T*)0)) {
            return s_pActiveRunEntry->task_group;
        }
        return &g_tUiTaskGroup;

    default:
        return (const TaskGroup_T*)0;
    }
}

/*
 * ============================================================================
 * SysRunEntryHook - 运行入口钩子
 * ============================================================================
 *
 * 作用：在进入主调度循环前，提供一个临时、低耦合的模块挂接点
 *
 * 使用场景：
 *   - 逐模块调试阶段，临时启用某个功能
 *   - 验证通过后再正式迁入 TASK 状态机
 *   - 保持 INIT 干净，TASK 干净
 *   - 可在里面进行一些 修改 外设地址 设置等 单词命令或一次性测试等功能函数
 *
 * 注意：这是过渡机制，不是长期架构主体,目前留空
 * --- 用于进行点灯测试, 直接进行驱动测试(如果发现一哥任务组异常的单独调试入口)
 * ============================================================================
 */
static void SysRunEntryHook(void)
{
}
