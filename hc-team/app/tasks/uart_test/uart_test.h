/**
 * @file    uart_test.h
 * @brief   DEBUG UART_TEST 调试服务对外接口定义
 *
 * 本模块对外提供 UART_TEST 运行项的初始化、运行态切换与周期遥测接口。
 *
 * 功能范围：
 * - 管理 4 个通用 float 调试量的 VOFA profile
 * - 提供运行项 Enter/Exit 生命周期接口
 * - 提供 10ms 周期遥测入口
 * - 不提供专用 OLED 渲染，运行页统一回落到默认 RUNNING
 *
 * 设计约定：
 * 1. 调试量固定命名为 U1/U2/U3/U4
 * 2. 所有串口输出统一复用 VOFA FireWater CSV
 * 3. 本模块不保存跨运行项的 VOFA profile，切入时总是重建
 */

#ifndef __UART_TEST_H__
#define __UART_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 公开 API ----------------------------------------------------------- */

/** 初始化 UART_TEST 模块本地状态。 */
void UartTest_Init(void);

/** 进入 UART_TEST 运行项并重建当前 VOFA profile。 */
void UartTest_Enter(void);

/** 退出 UART_TEST 运行项并清空当前 VOFA profile。 */
void UartTest_Exit(void);

/** UART_TEST 10ms 遥测入口。 */
void UartTest_Telemetry10ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_TEST_H__ */
