/**
 * @file    stepmotor_uart.h
 * @brief   UART_STEPPER_BUS 角色 Driver：步进电机总线的字节搬运层
 *
 * 模块职责：
 * - 拥有 UART_STEPPER_BUS 这一条链路的私有 RX FIFO
 * - 提供 TryWrite 与 TX 空闲/完成的可观测点，供上层做队列编排
 *
 * 本模块**不负责**：
 * - 不认识 EMM42 协议、不组包、不解析应答 —— 组包属 driver/step_motor/emm42，
 *   队列编排与应答处理属 app 的 stepmotor_bus
 * - 不做重试、不排队：IsTxIdle/ConsumeTxDone 只暴露事实，策略由上层定
 *
 * 硬件事实（board.syscfg 单源）：
 * - UART_STEPPER_BUS = UART7 @ 230400，收发均走 DMA
 */
#ifndef STEPMOTOR_UART_H
#define STEPMOTOR_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void StepmotorUart_Init(void);
uint32_t StepmotorUart_Read(uint8_t *out, uint32_t capacity);
bool StepmotorUart_TryWrite(const uint8_t *data, uint32_t length);
bool StepmotorUart_IsTxIdle(void);
bool StepmotorUart_ConsumeTxDone(void);
uint32_t StepmotorUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* STEPMOTOR_UART_H */
