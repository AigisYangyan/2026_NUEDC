/**
 * @file    task_groups.c
 * @brief   系统任务组与任务函数实现
 *
 * 本文件负责维护当前工程使用的任务组与周期任务入口。
 *
 * 功能范围：
 * - 提供统一 UI 任务组
 * - 提供统一 UI 周期任务
 * - 提供电机、编码器、视觉、VOFA 等任务入口
 *
 * 设计约定：
 * - 菜单核心、运行项注册与主状态机调度已拆分到独立模块
 * - INIT 状态不注册周期任务
 * - IDLE_PAGE 与未绑定专属任务组的 RUNNING 状态，统一回落到 UI 任务组
 */

#include <stdint.h>
#include <stdbool.h>
#include "app/tasks/task_groups.h"
#include "app/scheduler/task_scheduler.h"
#include "app/ui/oled/menu_core.h"
#include "app/tasks/gray_test/gray_test.h"
#include "app/tasks/speed_loop/speed_loop.h"
#include "app/tasks/uart_test/uart_test.h"
#include "app/tasks/uart_stress/uart_stress.h"
#include "app/tasks/task1/task1.h"
#include "driver/clock/clock.h"
#include "middleware/pid/pid.h"
#include "app/tasks/platform_2d/vision_coord.h"
#include "app/tasks/platform_2d/2DPlatform_LaserStrike.h"
#include "app/tasks/platform_2d/stepmotor_bus.h"
#include "app/tasks/platform_2d/vision_bus.h"
#include "driver/encoder/encoder.h"
#include "driver/key/key.h"
#include "driver/motor/motor.h"
#include "driver/oled/oled_hardware_i2c.h"
#include "driver/uart_vofa/uart_vofa.h"

/* ---- 周期配置 ----------------------------------------------------------- */

#define UI_TASK_PERIOD_MS 5u
#define SPEED_LOOP_SAMPLE_PERIOD_MS 10u
#define SPEED_LOOP_CONTROL_PERIOD_MS 10u
#define SPEED_LOOP_TELEMETRY_PERIOD_MS 20u
#define UART_TEST_TELEMETRY_PERIOD_MS 10u
#define GRAY_TEST_TELEMETRY_PERIOD_MS 10u
#define UART_STRESS_TICK_PERIOD_MS 5u
#define DEBUG_SMOOTH_CONTROL_PERIOD_MS 5u      /* DEBUG_Smooth 控制节拍 */
#define DEBUG_SMOOTH_TELEMETRY_PERIOD_MS 10u   /* DEBUG_Smooth 遥测节拍 */
#define DEBUG_VISION_DATA_TELEMETRY_PERIOD_MS 10u /* DEBUG_Vision_data 遥测节拍 */
#define VISION_TRACK_CONTROL_PERIOD_MS 5u      /* 二维平台内环控制节拍 */
#define VISION_TRACK_OUTER_PERIOD_MS 10u       /* 二维平台外环更新节拍 */
#define VISION_TRACK_TELEMETRY_PERIOD_MS 10u   /* 二维平台遥测节拍 */
#define TASK1_SAMPLE_PERIOD_MS 10u             /* TASK1 采样节拍 */
#define TASK1_CONTROL_PERIOD_MS 10u            /* TASK1 控制节拍 */
#define TASK1_TELEMETRY_PERIOD_MS 20u          /* TASK1 遥测节拍 */

/* ---- 调试与遥测通道变量 ------------------------------------------------- */

volatile float g_vofa_cmd_lm = 0.0f;
volatile float g_vofa_cmd_rm = 0.0f;
float g_vofa_vision_x = -1.0f;
float g_vofa_vision_y = -1.0f;

static Encoder_Snapshot s_encoder_snapshot;
static uint32_t s_encoder_last_ms;

/* VOFA 直控速度环的左右轮 PID 上下文：本文件持有，首次使用时初始化 */
static Pid_T s_vofa_left_pid;
static Pid_T s_vofa_right_pid;
static bool s_vofa_pid_ready = false;

static const Pid_Config_T s_vofa_pid_cfg = {
    .kp = 0.0f, .ki = 0.0f, .kd = 0.0f,
    .out_limit = 1000.0f,       /* PWM 输出尺度 ±1000，沿用原电机实例限幅 */
    .integral_limit = 0.0f,     /* 按 out_limit*3.5 推导 */
    .d_filter_alpha = 1.0f,     /* 不过滤 */
};

/* ---- 任务组定义与导出 --------------------------------------------------- */

/* 统一 UI 任务：挂到菜单浏览态与未接后台逻辑的运行项，保证系统运行过程中也可刷新 OLED。 */
static TaskComps_T s_tUiTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms }
};

#define TASK_COUNT(table) ((uint8_t)(sizeof(table) / sizeof((table)[0])))

const TaskGroup_T g_tUiTaskGroup = {
    s_tUiTasks,
    TASK_COUNT(s_tUiTasks)
};

/* SpeedLoop 首个 DEBUG 运行项专属任务组。
 * 说明：
 * 1. RUNNING 状态切入该运行项后，不再走默认 UI 占位组；
 * 2. 因此组内必须显式保留 UI 任务，保证 OLED/按键在运行中仍可用；
 * 3. 后续新增其它专属运行项时，按这个结构各自补一组即可。
 */
static TaskComps_T s_tSpeedLoopTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, SPEED_LOOP_SAMPLE_PERIOD_MS, SPEED_LOOP_SAMPLE_PERIOD_MS, Task_EncoderSpeedSample },
    { TASK_ENABLE, 0u, SPEED_LOOP_SAMPLE_PERIOD_MS, SPEED_LOOP_SAMPLE_PERIOD_MS, Task_SpeedLoopSample10ms },
    { TASK_ENABLE, 0u, SPEED_LOOP_CONTROL_PERIOD_MS, SPEED_LOOP_CONTROL_PERIOD_MS, Task_SpeedLoopControl10ms },
    { TASK_ENABLE, 0u, SPEED_LOOP_TELEMETRY_PERIOD_MS, SPEED_LOOP_TELEMETRY_PERIOD_MS, Task_SpeedLoopTelemetry20ms }
};

const TaskGroup_T g_tSpeedLoopTaskGroup = {
    s_tSpeedLoopTasks,
    TASK_COUNT(s_tSpeedLoopTasks)
};

static TaskComps_T s_tUartTestTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, UART_TEST_TELEMETRY_PERIOD_MS, UART_TEST_TELEMETRY_PERIOD_MS,
      Task_UartTestTelemetry10ms }
};

const TaskGroup_T g_tUartTestTaskGroup = {
    s_tUartTestTasks,
    TASK_COUNT(s_tUartTestTasks)
};

static TaskComps_T s_tGrayTestTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, GRAY_TEST_TELEMETRY_PERIOD_MS, GRAY_TEST_TELEMETRY_PERIOD_MS,
      Task_GrayTestSampleAndTelemetry10ms }
};

const TaskGroup_T g_tGrayTestTaskGroup = {
    s_tGrayTestTasks,
    TASK_COUNT(s_tGrayTestTasks)
};

/* UART_Stress 压测任务组：UI 5ms + 压测 Tick 5ms 双线。 */
static TaskComps_T s_tUartStressTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, UART_STRESS_TICK_PERIOD_MS, UART_STRESS_TICK_PERIOD_MS,
      Task_UartStressTick5ms }
};

const TaskGroup_T g_tUartStressTaskGroup = {
    s_tUartStressTasks,
    TASK_COUNT(s_tUartStressTasks)
};

/* DEBUG_Smooth 任务组：UI + 步进总线服务 + 速度模式控制 + VOFA 遥测。 */
static TaskComps_T s_tDebugSmoothTasks[] = {
        { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
        { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_StepmotorBusService5ms },
        { TASK_ENABLE, 0u, DEBUG_SMOOTH_CONTROL_PERIOD_MS, DEBUG_SMOOTH_CONTROL_PERIOD_MS,
            Task_DebugSmoothControl5ms },
        { TASK_ENABLE, 0u, DEBUG_SMOOTH_TELEMETRY_PERIOD_MS, DEBUG_SMOOTH_TELEMETRY_PERIOD_MS,
            Task_DebugSmoothTelemetry10ms }
};

const TaskGroup_T g_tDebugSmoothTaskGroup = {
        s_tDebugSmoothTasks,
        TASK_COUNT(s_tDebugSmoothTasks)
};

/* DEBUG_Vision_data 任务组：UI + 视觉数据 VOFA 遥测。 */
static TaskComps_T s_tDebugVisionDataTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_VisionBusService5ms },
    { TASK_ENABLE, 0u, DEBUG_VISION_DATA_TELEMETRY_PERIOD_MS,
        DEBUG_VISION_DATA_TELEMETRY_PERIOD_MS, Task_DebugVisionDataTelemetry10ms }
};

const TaskGroup_T g_tDebugVisionDataTaskGroup = {
    s_tDebugVisionDataTasks,
    TASK_COUNT(s_tDebugVisionDataTasks)
};

/* DEBUG_Stepper 任务组：UI + 视觉总线 + 步进总线 + 外环 + 内环 + 遥测。 */
static TaskComps_T s_tVisionTrackTasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_VisionBusService5ms },
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_StepmotorBusService5ms },
    { TASK_ENABLE, 0u, VISION_TRACK_OUTER_PERIOD_MS, VISION_TRACK_OUTER_PERIOD_MS,
      Task_VisionTrack10ms },
    { TASK_ENABLE, 0u, VISION_TRACK_CONTROL_PERIOD_MS, VISION_TRACK_CONTROL_PERIOD_MS,
      Task_VisionControl5ms },
    { TASK_ENABLE, 0u, VISION_TRACK_TELEMETRY_PERIOD_MS, VISION_TRACK_TELEMETRY_PERIOD_MS,
      Task_VisionTelemetry10ms }
};

const TaskGroup_T g_tVisionTrackTaskGroup = {
    s_tVisionTrackTasks,
    TASK_COUNT(s_tVisionTrackTasks)
};

/* TASK1（100x100 正方形巡线）任务组：UI + 采样 + 控制 + 遥测。*/
static TaskComps_T s_tTask1Tasks[] = {
    { TASK_ENABLE, 0u, UI_TASK_PERIOD_MS, UI_TASK_PERIOD_MS, Task_UiService5ms },
    { TASK_ENABLE, 0u, TASK1_SAMPLE_PERIOD_MS, TASK1_SAMPLE_PERIOD_MS, Task_EncoderSpeedSample },
    { TASK_ENABLE, 0u, TASK1_SAMPLE_PERIOD_MS, TASK1_SAMPLE_PERIOD_MS, Task_Task1Sample10ms },
    { TASK_ENABLE, 0u, TASK1_CONTROL_PERIOD_MS, TASK1_CONTROL_PERIOD_MS, Task_Task1Control10ms },
    { TASK_ENABLE, 0u, TASK1_TELEMETRY_PERIOD_MS, TASK1_TELEMETRY_PERIOD_MS, Task_Task1Telemetry20ms }
};

const TaskGroup_T g_tTask1TaskGroup = {
    s_tTask1Tasks,
    TASK_COUNT(s_tTask1Tasks)
};

/* ---- 任务函数实现 ------------------------------------------------------- */

/**
 * @brief 统一 UI 周期任务
 * @note  固定顺序执行：
 *        1. OLED 未 Ready 时推进非阻塞初始化
 *        2. 扫描按键并提取事件
 *        3. 将事件路由到菜单核心
 *        4. 仅当页面内容被标脏时才刷新界面
 */
void Task_UiService5ms(void)
{
    Key_Id_e key;
    bool oled_ready = OLED_IsReady();

    if (oled_ready == false) {
        (void)OLED_Process();
        oled_ready = OLED_IsReady();
    }

    Key_Scan();

    while (Key_PollPressEvent(&key) == true) {
        Menu_HandleKey(key);
    }

    if ((oled_ready == true) && (Menu_IsDirty() == true)) {
        Menu_RenderIfDirty();
    }
}

/**
 * @brief 编码器速度采样任务
 */
void Task_EncoderSpeedSample(void)
{
    uint32_t now_ms = Clock_NowMs();
    uint32_t elapsed_ms = now_ms - s_encoder_last_ms;

    if (elapsed_ms > 0u) {
        s_encoder_last_ms = now_ms;
        (void)Encoder_Update(elapsed_ms);
        Encoder_GetSnapshot(&s_encoder_snapshot);
    }
}

/**
 * @brief 电机 PID 闭环控制任务
 */
void Task_MotorPidControl(void)
{
    float left_out;
    float right_out;

    if (!s_vofa_pid_ready) {
        Pid_Init(&s_vofa_left_pid, &s_vofa_pid_cfg);
        Pid_Init(&s_vofa_right_pid, &s_vofa_pid_cfg);
        s_vofa_pid_ready = true;
    }

    left_out = Pid_UpdateIncremental(&s_vofa_left_pid,
                                     g_vofa_cmd_lm,
                                     s_encoder_snapshot.speed_mps[ENCODER_LEFT]);
    right_out = Pid_UpdateIncremental(&s_vofa_right_pid,
                                      g_vofa_cmd_rm,
                                      s_encoder_snapshot.speed_mps[ENCODER_RIGHT]);
    (void)Motor_SetOutput(MOTOR_LEFT, (int16_t)left_out);
    (void)Motor_SetOutput(MOTOR_RIGHT, (int16_t)right_out);
    Motor_Update(SPEED_LOOP_CONTROL_PERIOD_MS);
}

/**
 * @brief 步进电机总线 5ms 服务任务
 */
void Task_StepmotorBusService5ms(void)
{
    StepmotorBus_Service5ms();
}

/**
 * @brief 视觉总线 5ms 服务任务
 */
void Task_VisionBusService5ms(void)
{
    VisionBus_Service5ms();
}

/**
 * @brief 视觉跟踪 10ms 周期任务
 */
void Task_VisionTrack10ms(void)
{
    VisionHdl_Run10ms();
}

/**
 * @brief 二维平台 5ms 内环控制任务
 */
void Task_VisionControl5ms(void)
{
    VisionHdl_Control5ms();
}

/**
 * @brief 二维平台 10ms 遥测任务
 */
void Task_VisionTelemetry10ms(void)
{
    VisionHdl_Telemetry10ms();
}

/**
 * @brief VOFA 服务任务
 * @note  读取最新视觉坐标并更新上位机发送通道，随后执行 VOFA 协议服务
 */
void Task_VofaService(void)
{
    VisionCoord_Coordinates_t coord;

    if (VisionCoord_GetLatest(&coord) == true) {
        g_vofa_vision_x = (float)coord.x;
        g_vofa_vision_y = (float)coord.y;
    }
    else {
        g_vofa_vision_x = -1.0f;
        g_vofa_vision_y = -1.0f;
    }

    vofa_run();
}

/* -------------------------------------------------------------------------- */
/* SpeedLoop 专属任务封装                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief SpeedLoop 编码器采样任务
 */
void Task_SpeedLoopSample10ms(void)
{
    SpeedLoop_Sample10ms();
}

/**
 * @brief SpeedLoop 速度环控制任务
 */
void Task_SpeedLoopControl10ms(void)
{
    SpeedLoop_Control10ms();
}

/**
 * @brief SpeedLoop VOFA 遥测任务
 */
void Task_SpeedLoopTelemetry20ms(void)
{
    SpeedLoop_Telemetry20ms();
}

void Task_UartTestTelemetry10ms(void)
{
    UartTest_Telemetry10ms();
}

void Task_GrayTestSampleAndTelemetry10ms(void)
{
    GrayTest_SampleAndTelemetry10ms();
}

/**
 * @brief UART_Stress 压测 5ms 周期任务
 * @note  桥接到 UartStress_Tick5ms，由专属任务组调度
 */
void Task_UartStressTick5ms(void)
{
    UartStress_Tick5ms();
}

/**
 * @brief DEBUG_Smooth 控制封装任务
 * @note  由 DEBUG_Smooth 专属任务组以 5ms 周期调度
 */
void Task_DebugSmoothControl5ms(void)
{
    DebugSmooth_Control5ms();
}

/**
 * @brief DEBUG_Smooth 遥测封装任务
 * @note  由 DEBUG_Smooth 专属任务组以 10ms 周期调度
 */
void Task_DebugSmoothTelemetry10ms(void)
{
    DebugSmooth_Telemetry10ms();
}

/**
 * @brief DEBUG_Vision_data 遥测封装任务
 * @note  由 DEBUG_Vision_data 专属任务组以 10ms 周期调度
 */
void Task_DebugVisionDataTelemetry10ms(void)
{
    DebugVisionData_Telemetry10ms();
}

/* -------------------------------------------------------------------------- */
/* TASK1 任务包装                                                           */
/* -------------------------------------------------------------------------- */

void Task_Task1Sample10ms(void)
{
    Task1_Sample10ms();
}

void Task_Task1Control10ms(void)
{
    Task1_Control10ms();
}

void Task_Task1Telemetry20ms(void)
{
    Task1_Telemetry20ms();
}
