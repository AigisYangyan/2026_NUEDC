/**
 * @file    vision_uart.h
 * @brief   UART_VISION 角色 Driver：视觉模组串口的字节搬运层
 *
 * 模块职责：
 * - 拥有 UART_VISION 这一条链路的私有 RX FIFO
 * - ISR/DMA 只往 FIFO 里搬字节；上层在任务态 drain
 *
 * 本模块**不负责**：
 * - 不认识任何帧格式、不解析、不校验 —— 那属于 driver/uart_vision 编解码层
 * - 不做重试、不做超时策略；TryWrite/IsTxIdle/ConsumeTxDone 只暴露事实，策略由上层定
 *
 * 硬件事实（board.syscfg 单源，勿在此处硬编码）：
 * - UART_VISION = UART1 @ 230400，收发均走 DMA
 * - 实例号与引脚是本层以下的私有事实：换实例只改 syscfg，本文件不动
 */
#ifndef VISION_UART_H
#define VISION_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void VisionUart_Init(void);
uint32_t VisionUart_Read(uint8_t *out, uint32_t capacity);
bool VisionUart_TryWrite(const uint8_t *data, uint32_t length);
bool VisionUart_IsTxIdle(void);
bool VisionUart_ConsumeTxDone(void);
uint32_t VisionUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_UART_H */
