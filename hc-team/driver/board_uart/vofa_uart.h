/**
 * @file    vofa_uart.h
 * @brief   UART_HOST_LINK 角色 Driver：VOFA+ 上位机链路的字节搬运层
 *
 * 模块职责：
 * - 拥有 UART_HOST_LINK 这一条链路的私有 RX FIFO
 * - 提供有界的 TryWrite；ISR/DMA 只搬字节
 *
 * 本模块**不负责**：
 * - 不认识 JustFloat/FireWater 协议、不组包、不解析 —— 那属于 driver/uart_vofa
 * - RX 解析只允许发生在 vofa_run() 的任务上下文（违规 V09 的关闭条件）
 *
 * 硬件事实（board.syscfg 单源）：
 * - UART_HOST_LINK = UART5 @ 230400，收发均走 DMA
 * - ⚠ PA0/PA1 在当前板子上尚未引出，固件已就绪但实物不可用（待硬件组）
 */
#ifndef VOFA_UART_H
#define VOFA_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void VofaUart_Init(void);
uint32_t VofaUart_Read(uint8_t *out, uint32_t capacity);
bool VofaUart_TryWrite(const uint8_t *data, uint32_t length);
uint32_t VofaUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* VOFA_UART_H */
