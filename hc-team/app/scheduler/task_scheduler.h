/**
 * @file    task_scheduler.h
 * @brief   系统状态机与任务分组接口声明
 *
 * 本头文件定义系统状态枚举、任务描述结构与系统入口函数。
 *
 * 设计约定：
 * - SYS 状态只描述当前系统运行阶段
 * - 每个 SYS 状态对应一组静态登记任务
 * - 当前菜单页面不再直接等价于 SYS 状态
 */

#ifndef APP_SCHEDULER_TASK_SCHEDULER_H
#define APP_SCHEDULER_TASK_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "app/ui/oled/menu_core.h"

    /* ---- 状态机与任务类型 --------------------------------------------------- */

    /**
     * @brief 任务开关状态
     */
    typedef enum
    {
        TASK_DISABLE = 0,     /* 任务关闭 */
        TASK_ENABLE = 1       /* 任务启用 */
    } TaskSwitch_t;

    /**
     * @brief 系统状态标志
     * @note  采用“粗状态 + 当前运行项”统一方案：
     *        1. SYS 只描述当前大阶段，不再区分具体 TASK/DEBUG 运行项
     *        2. 具体运行内容由当前激活的 RunEntryReg_t 决定
     *        3. DEBUG 一级入口仍只负责页面跳转，不直接成为运行态
     */
    typedef enum
    {
        SYS_STA_INIT = 0,        /* 初始化，仅执行一次性初始化流程 */
        SYS_STA_IDLE_PAGE,       /* 菜单浏览态，仅运行 UI 任务 */
        SYS_STA_RUNNING,         /* 当前存在激活运行项 */
        SYS_STA_MAX_NUM          /* 状态机最大状态数 */
    } SYS_FLAG_TASK_E;

    extern SYS_FLAG_TASK_E g_eSysFlagManage;    /* 当前系统状态 */

    /**
     * @brief 单个任务描述符
     */
    typedef struct
    {
        uint8_t  Enable;            /* 任务开关（0=关闭，1=启用） */
        volatile uint32_t Run;      /* 时间片到达后置位，允许调度执行 */
        uint32_t TimCount;          /* 任务计时器当前计数值 */
        uint32_t TimRload;          /* 任务计时器重装值 */
        void (*pTaskFunc)(void);    /* 任务入口函数 */
    } TaskComps_T;

    /**
     * @brief 单个系统状态对应的任务组
     */
    typedef struct
    {
        TaskComps_T* pTaskList;     /* 当前状态机的任务表首地址 */
        uint8_t      TaskCount;     /* 当前状态机的任务数量 */
    } TaskGroup_T;

    /**
     * @brief 运行项 ID
     */
    typedef enum
    {
        RUN_ENTRY_NONE = 0,         /* 空运行项 */
        RUN_ENTRY_TASK1,            /* 一级菜单 TASK1 */
        RUN_ENTRY_TASK2,            /* 一级菜单 TASK2 */
        RUN_ENTRY_TASK3,            /* 一级菜单 TASK3 */
        RUN_ENTRY_TASK4,            /* 一级菜单 TASK4 */
        RUN_ENTRY_DEBUG_SPEED,      /* DEBUG -> Speed */
        RUN_ENTRY_DEBUG_UART_TEST,  /* DEBUG -> UART_TEST */
        RUN_ENTRY_DEBUG_UART_STRESS,/* DEBUG -> UART_Stress 230400/5ms 压测 */
        RUN_ENTRY_DEBUG_GRAY_TEST,  /* DEBUG -> GRAY_TEST */
        RUN_ENTRY_DEBUG_VISION_DATA,/* DEBUG -> Vision_data (backend: DEBUG_Vision_data) */
        RUN_ENTRY_DEBUG_TRACK,      /* DEBUG -> Track */
        RUN_ENTRY_DEBUG_STEPPER_X,  /* DEBUG -> Stepper_X */
        RUN_ENTRY_DEBUG_STEPPER_Y,  /* DEBUG -> Stepper_Y */
        RUN_ENTRY_DEBUG_MOTOR_SMOOTHNESS_SPEEDMODE_VELACC, /* DEBUG -> DEBUG_Smooth (backend: DEBUG_Motor_Smoothness_SpeedMode_VelAcc) */
        RUN_ENTRY_MAX_NUM           /* 运行项总数上限 */
    } RunEntryId_e;

    /**
     * @brief 运行项所属菜单分组
     */
    typedef enum
    {
        RUN_MENU_NONE = 0,          /* 不挂到菜单 */
        RUN_MENU_HOME,              /* 一级主菜单 */
        RUN_MENU_DEBUG              /* DEBUG 二级菜单 */
    } RunEntryMenuGroup_e;

    typedef void (*RunEntryHookFn)(void);

    /**
     * @brief 运行项统一注册描述符
     */
    typedef struct
    {
        RunEntryId_e        id;            /* 运行项 ID */
        const char*         name;          /* 菜单显示名称 */
        RunEntryMenuGroup_e menu_group;    /* 菜单分组 */
        MenuPageId_e        target_page;   /* 进入后的目标页面 */
        const TaskGroup_T*  task_group;    /* 运行态任务组，可为空表示默认 UI 占位 */
        MenuRenderFn        custom_render; /* 运行项自定义渲染 */
        RunEntryHookFn      on_enter;      /* 进入钩子 */
        RunEntryHookFn      on_exit;       /* 退出钩子 */
    } RunEntryReg_t;

    /* ---- 系统接口 ----------------------------------------------------------- */

    /**
     * @brief 系统初始化入口
     */
    void SysInit(void);

    /**
     * @brief 系统运行入口
     */
    void SysRun(void);

    /**
     * @brief 任务时间片推进函数
     * @note  需在 1ms 定时器中断中调用
     */
    void TaskTimeSliceManage(void);

    const RunEntryReg_t* Sys_GetActiveRunEntry(void);
    bool Sys_EnterRunEntry(RunEntryId_e id);
    void Sys_LeaveRunEntry(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_TASK_SCHEDULER_H */
