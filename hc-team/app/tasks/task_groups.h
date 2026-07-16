/**
 * @file    task_groups.h
 * @brief   任务函数对外接口声明
 *
 * 本头文件声明系统当前已实现的任务组对象与周期任务入口。
 *
 * 功能范围：
 * - 导出各运行项对应的静态任务组
 * - 导出菜单 UI、速度环、灰度测试、串口测试、二维平台等任务入口
 * - 为调度器提供统一的任务函数声明
 *
 * 不负责的内容：
 * - 任务组的具体静态表定义
 * - 系统状态切换与运行项注册
 * - 任务内部的业务逻辑实现
 *
 * 设计约定：
 * 1. 任务注册关系由 task_groups.c 中的任务组表统一维护
 * 2. 头文件只导出任务组和任务入口，不在这里持有运行时状态
 * 3. 所有周期任务由调度器按固定节拍驱动，不在接口层重复描述时间片逻辑
 */

#ifndef APP_TASKS_TASK_GROUPS_H
#define APP_TASKS_TASK_GROUPS_H

#include "app/scheduler/task_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* ---- 任务组导出 --------------------------------------------------------- */

    extern const TaskGroup_T g_tUiTaskGroup;
    extern const TaskGroup_T g_tSpeedLoopTaskGroup;
    extern const TaskGroup_T g_tUartTestTaskGroup;
    extern const TaskGroup_T g_tGrayTestTaskGroup;
    extern const TaskGroup_T g_tUartStressTaskGroup;
    /* DEBUG_Smooth 专属任务组（UI + 总线服务 + 控制 + 遥测）。 */
    extern const TaskGroup_T g_tDebugSmoothTaskGroup;
    /* DEBUG_Vision_data 专属任务组（UI + 视觉数据遥测）。 */
    extern const TaskGroup_T g_tDebugVisionDataTaskGroup;
    /* DEBUG_Stepper 专属任务组（UI + 视觉总线 + 步进总线 + 串级控制 + 遥测）。 */
    extern const TaskGroup_T g_tVisionTrackTaskGroup;
    /* TASK1 专属任务组（UI + 10ms 采样 + 10ms 控制 + 20ms 遥测）。 */
    extern const TaskGroup_T g_tTask1TaskGroup;

    /* ---- 周期任务接口 ------------------------------------------------------- */

    /**
     * @brief UI 服务任务
     * @note  推进 OLED、扫描按键、路由菜单并执行按需渲染
     */
    void Task_UiService5ms(void);

    /**
     * @brief 编码器速度采样任务
     */
    void Task_EncoderSpeedSample(void);

    /**
     * @brief 电机 PID 闭环控制任务
     */
    void Task_MotorPidControl(void);

    /**
     * @brief 步进电机总线 5ms 服务任务
     */
    void Task_StepmotorBusService5ms(void);

    /**
     * @brief 视觉总线 5ms 服务任务
     */
    void Task_VisionBusService5ms(void);

    /**
     * @brief 视觉跟踪 10ms 周期任务
     */
    void Task_VisionTrack10ms(void);

    /**
     * @brief 二维平台 5ms 内环控制任务
     */
    void Task_VisionControl5ms(void);

    /**
     * @brief 二维平台 10ms 遥测任务
     */
    void Task_VisionTelemetry10ms(void);

    /**
     * @brief VOFA 通信与遥测任务
     */
    void Task_VofaService(void);

    /**
     * @brief SpeedLoop 编码器采样任务
     */
    void Task_SpeedLoopSample10ms(void);

    /**
     * @brief SpeedLoop 速度环控制任务
     */
    void Task_SpeedLoopControl10ms(void);

    /**
     * @brief SpeedLoop VOFA 遥测任务
     */
    void Task_SpeedLoopTelemetry20ms(void);

    /**
     * @brief UART_TEST 10ms 遥测任务
     */
    void Task_UartTestTelemetry10ms(void);

    /**
     * @brief GRAY_TEST 10ms 采样与遥测任务
     */
    void Task_GrayTestSampleAndTelemetry10ms(void);

    /**
     * @brief UART_Stress 5ms 压测任务
     */
    void Task_UartStressTick5ms(void);

    /**
     * @brief DEBUG_Smooth 5ms 控制任务
     * @note  推进速度模式平顺性状态机并下发双轴速度命令
     */
    void Task_DebugSmoothControl5ms(void);

    /**
     * @brief DEBUG_Smooth 10ms 遥测任务
     * @note  输出 V_cmd/A_cmd/ack_or_err 并驱动 VOFA 协议发送
     */
    void Task_DebugSmoothTelemetry10ms(void);

    /**
     * @brief DEBUG_Vision_data 10ms 遥测任务
     * @note  输出 pixel_err_X/pixel_err_Y/frame_dt_ms/status 并驱动 VOFA 发送
     */
    void Task_DebugVisionDataTelemetry10ms(void);

    /**
     * @brief TASK1 10ms 采样任务 (编码器 + 灰度 + IMU 角速度)
     */
    void Task_Task1Sample10ms(void);

    /**
     * @brief TASK1 10ms 控制任务 (状态机推进 + 电机输出)
     */
    void Task_Task1Control10ms(void);

    /**
     * @brief TASK1 20ms 遥测任务 (VOFA 推送)
     */
    void Task_Task1Telemetry20ms(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_TASK_GROUPS_H */
