/**
 * @file    wireless_uart.h
 * @brief   ESP32-C3（ESP-NOW 透传伴侣）UART 端口层接口。
 *
 * **占位实现**（WL1 契约 §35）：引脚未定案（硬件对接确认清单 H5/H6——UART5 与 VOFA
 * 共用被硬件标冲突，PA15 标绿备选；固件侧 UART2/4/6 空闲）。定案后本 .h 接口不变，
 * wireless_uart.c 换成真实 syscfg UART 实例包装（vofa_uart/vision_uart 同款），
 * 上层 uart_wireless/link 零改动。约定波特率 115200 8N1（H6 待确认）。
 */
#ifndef WIRELESS_UART_H
#define WIRELESS_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 端口初始化。占位实现恒 false（端口缺席）。 */
bool WirelessUart_Init(void);

/** @brief 读走 RX FIFO 中至多 cap 字节，返回实读数。占位恒 0。 */
uint32_t WirelessUart_Read(uint8_t *buf, uint32_t cap);

/** @brief 发送 len 字节。占位恒 false。 */
bool WirelessUart_Write(const uint8_t *data, uint32_t len);

/** @brief RX FIFO 溢出丢弃字节累计（四个既有 board_uart 端口同款口）。占位恒 0。 */
uint32_t WirelessUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* WIRELESS_UART_H */
