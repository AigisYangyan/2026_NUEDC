/**
 * @file    wireless_uart.h
 * @brief   ESP32-C3（ESP-NOW 透传伴侣）UART 端口层接口。
 *
 * **占位实现**（WL1 契约 §35）：引脚未定案（硬件对接确认清单 H5/H6——UART5 与 VOFA
 * 共用被硬件标冲突，PA15 标绿备选；固件侧 UART2/4/6 空闲）。定案后本 .h 接口不变，
 * wireless_uart.c 换成真实 syscfg UART 实例包装（vofa_uart/vision_uart 同款），
 * 上层 uart_wireless/link 零改动。约定波特率 115200 8N1（H6 待确认）。
 *
 * **真端口实现规格**（WL2 契约 §38，照抄 vofa_uart.c 全套，勿自创）：
 * - RX：私有字节环 FIFO + ISR 只搬运（IsrPushByte）+ 溢出计数。容量公式
 *   `baud/10 × 2×服务周期 × 安全系数2`：115200/10 × 20ms × 2 ≈ 460 → **512B**；
 * - TX：软件字节环 FIFO（忙时入队不丢帧）+ DMA kick 链（TryWrite 持锁 kick、
 *   IsrTxDone 清 busy 续段）+ 溢出计数，环容量 ≤ DMA 发送缓冲（**512B/512B**，
 *   编译期断言同款）；无 DMA 则 IRQ 逐字节排空同一个环；
 * - ISR 只做读清/搬运/计数/置位；中断分派进 mspm0_runtime 固定表（无回调注册）；
 * - 帧级可靠性（seq/ACK/重传）全在 uart_wireless 协议层，端口层零协议知识。
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
