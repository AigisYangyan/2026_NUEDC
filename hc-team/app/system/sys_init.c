/**
 * @file    sys_init.c
 * @brief   系统初始化实现
 *
 * 本文件负责系统启动阶段的一次性初始化编排。
 *
 * 功能范围：
 * - 调用 SysConfig 根初始化与本地 MSPM0 runtime
 * - 初始化 board_uart 角色驱动、电机、编码器、菜单、VOFA、视觉和调试任务模块
 * - 在所有模块 ready 后开启 NVIC/全局中断
 *
 * 不负责的内容：
 * - 主循环调度与任务时间片推进
 * - 运行项切换与菜单按键处理
 * - 各业务任务的周期执行逻辑
 *
 * 实现说明：
 * 1. 初始化顺序按“SysConfig/runtime -> 驱动层 -> 中间件 -> app 层任务”展开
 * 2. 所有运行态 profile 和任务组只在这里做一次性初始化，不在 Enter 时重复做模块初始化
 * 3. UART 角色归属在 driver/board_uart 固化；运行态切换不再改底层 ISR 归属
 */

#include "app/scheduler/task_scheduler.h"
#include "app/scheduler/vofa_register.h"
#include "app/ui/oled/menu_core.h"
#include "driver/board/board.h"
#include "driver/clock/clock.h"
#include "app/tasks/gray_test/gray_test.h"
#include "app/tasks/speed_loop/speed_loop.h"
#include "app/tasks/uart_test/uart_test.h"
#include "app/tasks/uart_stress/uart_stress.h"
#include "app/tasks/platform_2d/2DPlatform_LaserStrike.h"
#include "driver/board_uart/imu_uart.h"
#include "driver/board_uart/stepmotor_uart.h"
#include "driver/board_uart/vision_uart.h"
#include "driver/board_uart/vofa_uart.h"
#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "middleware/pid/pid.h"
#include "driver/encoder/encoder.h"
#include "driver/key/key.h"
#include "driver/motor/motor.h"
#include "driver/oled/oled_hardware_i2c.h"
#include "app/tasks/task1/task1.h"
#include "app/tasks/platform_2d/stepmotor_bus.h"
#include "app/tasks/platform_2d/vision_bus.h"
#include "driver/uart_vofa/uart_vofa.h"

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 系统初始化
 * @note  编排顺序：Board -> Clock -> Driver Init -> Middleware/App Init -> 开中断。
 *        Board Driver 是唯一接触 SYSCFG_DL_init/NVIC/__enable_irq 的项目代码。
 */
void SysInit(void)
{
    /* ---- 板级与基础外设初始化（全局中断未开启） -------------------------- */

    g_eSysFlagManage = SYS_STA_INIT;

    Board_Init();
    Clock_Init();
    Mspm0Runtime_InitUartDma();
    VisionUart_Init();
    VofaUart_Init();
    StepmotorUart_Init();
    ImuUart_Init();

    OLED_Init();
    Key_Init();
    Menu_Init();

    /* ---- 驱动层与中间件初始化 ------------------------------------------ */

    Motor_Init(); /* 内部完成安全态和 PWM 计数器启动。 */
    Encoder_Init();
    pid_Init();
    VofaRegister_Init();
    Motor_BrakeAll();
    StepmotorBus_Init();
    VisionBus_Init();
    vofa_init();

    /* ---- app 层任务模块初始化 ------------------------------------------ */

    UartTest_Init();
    UartStress_Init();
    GrayTest_Init();
    SpeedLoop_Init();
    VisionHdl_Init();
    Task1_Init();

    /* ---- 中断使能（所有 Driver 初始化完成后） --------------------------- */

    Board_EnableInterrupts();
}
