/**
 * @file    imu_uart.h
 * @brief   UART_IMU 角色端口：单轴 IMU 模组的字节级收发。
 *
 * 本模块只搬运字节，不认识帧、校验和或寄存器 —— 协议归 driver/imu。
 * RX 走中断：mspm0_runtime 的 UART3 IRQ 把字节推进本模块私有 FIFO，
 * 上层在任务态用 ImuUart_Read() 取走（AGENTS.md §5：ISR 只做最小搬运）。
 */
#ifndef IMU_UART_H
#define IMU_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 清空 RX FIFO 与溢出计数。须在全局中断开启前调用。 */
void ImuUart_Init(void);

/**
 * @brief 以有界轮询把 length 个字节写入 UART_IMU。
 * @param data   待发送缓冲区；调用返回后调用者可立即复用。
 * @param length 字节数；为 0 或 data 为 NULL 时返回 false。
 * @return 全部字节是否都已交给发送 FIFO；任一字节等待超时即 false。
 * @note  阻塞，无 DMA。IMU 写指令为 5 字节，最坏约 0.5 ms。
 */
bool ImuUart_TryWrite(const uint8_t *data, uint32_t length);

/**
 * @brief 从私有 RX FIFO 取走至多 capacity 个字节。
 * @param out      输出缓冲区；为 NULL 或 capacity 为 0 时返回 0。
 * @param capacity out 的容量。
 * @return 实际取走的字节数；FIFO 空时返回 0。
 * @note  任务态调用。内部用 PRIMASK 临界区与 IRQ 推入互斥。
 */
uint32_t ImuUart_Read(uint8_t *out, uint32_t capacity);

/**
 * @brief 返回因 FIFO 满而被丢弃的累计字节数。
 * @note  持续增长说明 ImuUart_Read() 调用过疏，或器件波特率与配置不符
 *        导致收到远超预期的字节。
 */
uint32_t ImuUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* IMU_UART_H */
