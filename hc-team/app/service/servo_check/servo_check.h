/**
 * @file    servo_check.h
 * @brief   舵机测试/标定薄服务（能力：舵机能被手动摆位与释放，SV1 契约 §34 修订 1）。
 *
 * 本服务是**操作员暂存角**的唯一所有者（默认 90°，未按过按钮=未设置）：
 * 空转期 SERVO 参数组改暂存值（不触 driver），进 ServoTest 条目时施加已设置的路。
 * 工作流=改角→进页看→BACK 释放→再改再进（菜单 RUN_ACTIVE 锁死约束下的标定环）。
 * 限幅/斜坡/换算全归 driver/servo（§8.2）；不入 flash（赛题定型后随 schema v3）。
 */
#ifndef SERVO_CHECK_H
#define SERVO_CHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Servo_Init 复位 + 对已设置的路施加暂存角（从未设置则保持自由态零命令）。 */
void ServoCheck_Start(void);

/** @brief 泵 Servo_Update（10ms 门控在 driver）。 */
void ServoCheck_Update(uint32_t now_ms);

/** @brief Servo_Disable ×2——释放（测试语境，不持载荷）。 */
void ServoCheck_Stop(void);

/* SERVO 参数组行（int 度口径）：读写操作员暂存角；空转期不触 driver，
 * 会话活动中立即透传（夹域仍归 driver 唯一限幅点）。 */
int32_t ServoCheck_GetS1Deg(void);
void    ServoCheck_SetS1Deg(int32_t deg);
int32_t ServoCheck_GetS2Deg(void);
void    ServoCheck_SetS2Deg(int32_t deg);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CHECK_H */
