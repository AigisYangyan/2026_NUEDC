/**
 * @file    gray_port.h
 * @brief   12 路灰度 Driver 的硬件边界（HAL port）
 *
 * 本头是 gray.c（散射逻辑，可主机测试）与 gray_hw.c（唯一允许碰 DL HAL 的实现）
 * 之间的唯一契约，范式与 motor.h / motor_hw.h 一致。
 *
 * 主机测试用 tests/host/fake_gray_port.c 顶替 gray_hw.c，从而在不接触 TI HAL 的
 * 前提下验证「一次读取 + 位散射」这一核心行为。
 *
 * 本头**不属于**公共 API：上层只应包含 gray.h。
 */
#ifndef HC_TEAM_DRIVER_GRAY_GRAY_PORT_H
#define HC_TEAM_DRIVER_GRAY_GRAY_PORT_H

#include <stdint.h>

/**
 * @brief  一次读回 12 路灰度所在端口的原始电平。
 *
 * 实现必须用**单次**端口读取取回全部 12 路（12 路已由 board.syscfg 约束在同一
 * 端口）。分多次读会在路间引入时间偏斜，使同一位图里的位来自不同时刻。
 *
 * @return 端口原始值，仅 12 路对应的位有意义，其余位由实现掩掉。
 */
uint32_t gray_port_read(void);

/**
 * @brief  取通道 channel 在端口原始值中的位掩码。
 *
 * 通道号到端口位的映射是**接线事实**，唯一来源是 board.syscfg 生成的
 * GPIO_LINE_SENSOR_PIN_IN*_PIN 宏。gray.c 不得对该映射作任何假设
 * （尤其不得假设 channel i 就是端口的 bit i —— 实际接线并非如此）。
 *
 * @param channel 通道号，[0, GRAY_CHANNEL_COUNT)。越界返回 0。
 * @return 该通道的端口位掩码。
 */
uint32_t gray_port_channel_mask(uint32_t channel);

#endif /* HC_TEAM_DRIVER_GRAY_GRAY_PORT_H */
