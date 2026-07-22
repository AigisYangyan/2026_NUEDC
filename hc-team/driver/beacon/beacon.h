/**
 * @file    beacon.h
 * @brief   声光指示 Driver：蜂鸣器（PB18）与状态 LED（PB22）的引脚开关唯一所有者。
 *
 * 只做电平开关，无节拍/时序逻辑（节拍归 app/service/alert 唯一所有）。
 * 蜂鸣器假设有源（电平开关即响）；无源需换 PWM 实现，接口不变——见
 * docs/硬件对接确认清单.md H4。
 *
 * 调用上下文：全部任务态；无 ISR、无缓冲、无状态机。
 */
#ifndef BEACON_H
#define BEACON_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 蜂鸣器/LED 全灭。上电静默由 syscfg 输出初值低已保证（生成代码先 clearPins
 *  再 enableOutput）；本函数供不经 SYSCFG 路径（主机测试/复位重入）显式归零用。 */
void Beacon_Init(void);

/** @brief 蜂鸣器开关。true=响（有源假设）。 */
void Beacon_SetBuzzer(bool on);

/** @brief 状态 LED 开关。true=亮。 */
void Beacon_SetLed(bool on);

#ifdef __cplusplus
}
#endif

#endif /* BEACON_H */
