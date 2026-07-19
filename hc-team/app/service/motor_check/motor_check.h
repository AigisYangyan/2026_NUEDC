/**
 * @file    motor_check.h
 * @brief   电机方向自检服务（App Service 层）——两轮同向前/后定时循环。
 *
 * 抽象（底盘器件能做什么）：
 * - 启动/推进/停止一次「两轮同向前进 2s → 后退 2s → 循环」方向自检。
 *
 * 用途：查 TB6612 AI1/AI2 是否接反——两轮应同向同幅；某轮反转即该轮方向线接反，肉眼可辨。
 *
 * 隐藏：相位状态机、时长常量、输出幅值、时间基准。
 *
 * 分层与所有权（AGENTS.md §4/§8.1）：
 * - 换向过零 + 5ms 死区、slew 限速、100ms 命令超时归零、刹车真值表全部唯一在 `motor.c`（V12）；
 *   本服务只发 ±输出目标 + 每拍刷新命令（防超时）+ 推进 Motor_Update + Stop 时 Motor_BrakeAll，
 *   **不复做任何限幅/换向/超时/slew 逻辑**。相位翻转只改目标，反向的过零/死区由 motor.c 承担。
 *
 * 安全（§8.1）：Start 不立即发命令（首 Update 播种）；Stop=确定性 Motor_BrakeAll；
 * 开机安全态由 System 装配层 Motor_Init+Motor_BrakeAll 保证（本服务不改装配）。
 *
 * 调用前置条件（System 装配层负责）：已完成 `Motor_Init()`。
 */
#ifndef MOTOR_CHECK_H
#define MOTOR_CHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 启动自检：相位复位=FORWARD、时间基准待播种。不立即发命令（首 Update 播种）。 */
void MotorCheck_Start(void);

/**
 * @brief 周期推进：首拍播种时间基准并起动 FORWARD 相位输出；此后每拍以当前相位 ±输出
 *        刷新两轮命令（防 motor.c 100ms 超时）并 Motor_Update(elapsed) 推进状态机；
 *        相位累计到 2000ms 翻转 FORWARD↔BACKWARD 循环。反向的过零/死区归 motor.c。
 * @param now_ms System 装配层供给的毫秒时刻（经 scheduler on_step 注入）。
 */
void MotorCheck_Update(uint32_t now_ms);

/** 停止自检：Motor_BrakeAll（确定性停止，§8.1）。 */
void MotorCheck_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_CHECK_H */
