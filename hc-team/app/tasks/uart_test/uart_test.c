/**
 * @file    uart_test.c
 * @brief   DEBUG UART_TEST 调试服务实现
 *
 * 本文件实现 UART_TEST 运行项的 VOFA 调试量管理。
 *
 * 功能范围：
 * - 管理 4 个可通过 VOFA 命令修改的通用 float 调试量
 * - 在进入运行项时重建 UART_TEST 专属 VOFA profile
 * - 以固定周期发送当前 4 个调试量
 * - 不提供专用 OLED 显示，运行页统一显示默认 RUNNING
 *
 * 不负责的内容：
 * - 自定义串口协议或额外帧格式
 * - 与电机、循迹等业务逻辑直接耦合
 *
 * 实现说明：
 * 1. 本模块调试量无业务语义，仅用于链路验证与丢帧分析
 * 2. Enter/Exit 期间会清空并重建当前 VOFA profile，避免与其他 DEBUG 子任务互相污染
 * 3. 遥测任务只负责推进 vofa_run()，不做额外状态机
 */

#include "app/tasks/uart_test/uart_test.h"
#include "app/scheduler/vofa_register.h"
#include "driver/uart_vofa/uart_vofa.h"

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief UART_TEST 模块初始化
 * @note  仅清零本地调试量，不注册运行态 profile
 */
void UartTest_Init(void)
{
    VofaUartTestCtx_t* ctx = VofaRegister_GetUartTestCtx();

    ctx->cmd_u1 = 0.0f;
    ctx->cmd_u2 = 0.0f;
    ctx->cmd_u3 = 0.0f;
    ctx->cmd_u4 = 0.0f;
}

/**
 * @brief 进入 UART_TEST 运行项
 * @note  清空旧 profile 后重建当前 4 通道 profile
 */
void UartTest_Enter(void)
{
    VofaRegister_EnterProfile(VOFA_PROFILE_UART_TEST);
}

/**
 * @brief 退出 UART_TEST 运行项
 */
void UartTest_Exit(void)
{
    VofaRegister_ExitProfile();
}

/**
 * @brief UART_TEST 10ms 遥测任务入口
 */
void UartTest_Telemetry10ms(void)
{
    vofa_run();
}
