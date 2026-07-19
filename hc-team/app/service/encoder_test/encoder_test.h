/**
 * @file    encoder_test.h
 * @brief   编码器脉冲遥测诊断服务（App Service 层）——只读、只发 VOFA。
 *
 * 抽象（诊断能做什么）：
 * - 进入/退出一次编码器脉冲遥测（进入即挂 VOFA tx 组、退出清组）；
 * - 被周期推进：自泵编码器采样 + 把左右轮累计脉冲/线速度发上位机。
 *
 * 用途：手动转轮观察左右轮接线正负（正转脉冲应正增，不应出现正转脉冲反号）；
 *       并实测「脉冲环测 100 米」的累计脉冲值。
 *
 * 隐藏：
 * - 用了哪些 Driver、VOFA 变量组存储、采样/发帧节奏、通道顺序。
 *
 * 分层与所有权（AGENTS.md §4/§8.2）：
 * - 编码器方向修正唯一在 `encoder.c s_direction_sign[]`——本服务读已修正快照，不加第二反向；
 * - 单位换算（脉冲→m/s）唯一在 encoder.c——本服务只单向复制快照到 tx，不做第二处理；
 * - VOFA 协议/解析/缓冲归 uart_vofa Driver。本服务唯一拥有：诊断 tx 组 + 采样/发帧节奏。
 * - Encoder_Update 采样点：本服务是继 chassis 之后的第二个调用点（诊断采样，仅本条目活动期）。
 *   单活动条目不变量（scheduler）保证与 chassis 永不同拍——V21「多推进点、互斥缓解」同款模式。
 *
 * 调用前置条件（System 装配层负责）：
 * - 已完成 `Encoder_Init()` 与 `vofa_init()`（含 VofaUart_Init）。
 */
#ifndef ENCODER_TEST_H
#define ENCODER_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 进入诊断：清 VOFA profile → 注册 tx×4（无 cmd）→ 采样基准待播种。不发任何电机命令。 */
void EncoderTest_Start(void);

/**
 * @brief 周期推进：自门控 10ms（now_ms 无符号减法）。首拍只播种基准；
 *        到期执行 Encoder_Update(elapsed) → 刷新 tx 快照 → vofa_run（发本拍刷新帧，无一帧延迟）。
 * @param now_ms System 装配层供给的毫秒时刻（经 scheduler on_step 注入）。
 */
void EncoderTest_Update(uint32_t now_ms);

/** 退出诊断：清 VOFA profile。本服务从不驱动电机，无电机需停。 */
void EncoderTest_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_TEST_H */
