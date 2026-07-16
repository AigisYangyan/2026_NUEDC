/**
 * @file    vision_bus.h
 * @brief   视觉坐标串口总线服务模块对外接口
 *
 * 本模块负责 UART_VISION (UART1, 230400) 上的视觉坐标帧接收与分流。
 *
 * 功能范围：
 * - 维护视觉 UART 的接收 FIFO
 * - 基于 {0x55 0xAA cmd len ...} 协议头做字节级分帧
 * - 把完整帧交给 VisionCoord_HandleFrame 做业务解释
 *
 * 设计约定：
 * - 视觉协议独占 UART1，不再与步进电机共享
 * - 接收侧先入 FIFO，再由 5ms 周期任务完成协议识别
 * - 本模块纯 RX，不对视觉上位机发送数据
 */

#ifndef VISION_BUS_H
#define VISION_BUS_H


#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- 公开 API ----------------------------------------------------------- */

/* 初始化视觉总线状态（清 FIFO，同时 VisionCoord_Init 由本模块兜底调用）。 */
void VisionBus_Init(void);

/* 供 runtime UART 直接注册的视觉接收回调。 */
void VisionBus_RxISR(uint8_t data);

/* 由 5ms 周期任务调用，推进视觉帧解析。 */
void VisionBus_Service5ms(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_BUS_H */
