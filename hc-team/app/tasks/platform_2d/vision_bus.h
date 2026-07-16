/**
 * @file    vision_bus.h
 * @brief   视觉坐标串口总线服务模块对外接口
 *
 * 本模块负责 UART_VISION (UART3, 230400) 上的视觉坐标帧拉取与分流。
 *
 * 功能范围：
 * - 从 VisionUart Driver 批量拉取字节
 * - 基于 {0x55 0xAA cmd len ...} 协议头做任务态分帧
 * - 把完整帧交给 VisionCoord_HandleFrame 做业务解释
 */

#ifndef VISION_BUS_H
#define VISION_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

void VisionBus_Init(void);
void VisionBus_Service5ms(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_BUS_H */
