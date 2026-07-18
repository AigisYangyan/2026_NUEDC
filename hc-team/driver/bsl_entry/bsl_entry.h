/**
 * @file    bsl_entry.h
 * @brief   UART0 BSL 软件跳转入口 Driver —— 运行期收到 0x22 → 跳官方 ROM BSL
 *
 * 模块职责：
 * - 监听 UART_BSL_ENTRY（UART0/PA10/PA11/9600）逐字节输入
 * - 命中触发字节 0x22 → 触发软件跳转到 MSPM0 官方 ROM BSL（永不返回，设备复位）
 *
 * 本模块**不负责**：
 * - 不搬运/不缓冲字节流（本口不承载数据，只逐字节判触发）
 * - 不设超时/重试；不暴露电平态
 *
 * 硬件事实（board.syscfg 单源，勿在此处硬编码）：
 * - UART_BSL_ENTRY = UART0 @ 9600，逐字节 RX 中断，无 DMA
 * - NVIC 使能唯一在 driver/board/board.c；外设/引脚/波特率唯一在 board.syscfg
 *
 * 触发时机（契约 D14 裁定）：ISR 内直接触发。这是对 V09/ISR-最小 规则的一处显式豁免，
 * 正当性 = 跳转即复位、永不返回、无返回栈、无共享态竞争。详见 contract_D14_bsl_entry.md。
 */
#ifndef BSL_ENTRY_H
#define BSL_ENTRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  ISR 上下文回调：runtime 把 UART_BSL_ENTRY 收到的每个字节喂进来。
 * @param  byte 收到的字节。
 * @note   命中触发字节（0x22）→ 调 BslEntry_InvokeBsl()，永不返回；否则立即返回。
 *         签名匹配 runtime_handle_uart_irq 的 void(*)(uint8_t) 回调契约。
 */
void BslEntry_IsrOnByte(uint8_t byte);

/**
 * @brief  擦除 SRAM 后复位进官方 ROM BSL，**永不返回**。
 * @note   target 定义在 bsl_entry_invoke.c（含 BSL_ERR_01 勘误绕行）；
 *         主机测试由 fake_bsl_invoke.c 替身计数（asm 硬件边界不可主机验证）。
 */
void BslEntry_InvokeBsl(void);

#ifdef __cplusplus
}
#endif

#endif /* BSL_ENTRY_H */
