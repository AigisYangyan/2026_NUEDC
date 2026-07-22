/**
 * @file    servo.h
 * @brief   舵机 Driver：角度域状态机（限幅/斜坡唯一所有者，SV1 契约 §34）。
 *
 * 硬件：50Hz 标准舵机 PWM，500~2500µs ↔ 0~180°；舵机1=PA27(TIMG7_CCP1)、
 * 舵机2=PB1(TIMG0_CCP1)，均 2.5MHz 计数时钟（生成代码核对过 50Hz）。
 *
 * §8.1 安全模型：
 * - Init/复位 = 比较值 0 = 无脉冲 = 自由态（舵机不上力，可被外力回驱）。
 * - 软限位与斜坡速率唯一在本模块；上层（服务/菜单）零复做。
 * - 自由态首命令 = 播种当前角=目标（当前物理角未知，舵机内环自会全速走位）
 *   ——**首命令请给安全位**；此后命令按斜坡速率推进。
 * - Servo_Disable = 确定性释放接口（停脉冲）。
 * - **刻意无命令超时**：舵机是位置保持器件，保持即安全态；超时释放反而摔载荷
 *   （显式偏离 §8.1 超时条款，契约 §34 登记）。
 *
 * 调用上下文：全部任务态。
 */
#ifndef SERVO_H
#define SERVO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SERVO_1 = 0, SERVO_2, SERVO_COUNT } Servo_Id;

/** @brief 状态复位 + 计数器起振（比较 0=无脉冲）；不出任何角度命令。 */
void Servo_Init(void);

/**
 * @brief 设软限位（默认 0..180）。域 [0,180] 且 min<max，否则拒绝返回 false。
 * @note  收窄时若目标越界会被立即夹回新域。唯一限幅点。
 */
bool Servo_SetLimitsDeg(Servo_Id id, float min_deg, float max_deg);

/** @brief 设斜坡速率（度/秒，>0，默认 300）；非法拒绝返回 false。 */
bool Servo_SetRateDegPerS(Servo_Id id, float rate);

/**
 * @brief 命令目标角（夹软限位）。自由态首命令播种当前角=目标（见文件头）。
 * @return id 越界返回 false；夹限后恒接受。
 */
bool Servo_SetTargetDeg(Servo_Id id, float deg);

/** @brief 10ms 自门控：斜坡推进当前角 → 写脉宽（唯一硬件写点）。now_ms 注入。 */
void Servo_Update(uint32_t now_ms);

/** @brief 停脉冲 = 自由态。再次 SetTargetDeg 会重新播种（不从陈旧角斜坡）。 */
void Servo_Disable(Servo_Id id);

/** @brief 是否在出脉冲保持位置。 */
bool Servo_IsActive(Servo_Id id);

/** @brief 当前斜坡角；自由态返回最后角（仅供显示）。 */
float Servo_GetAngleDeg(Servo_Id id);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H */
