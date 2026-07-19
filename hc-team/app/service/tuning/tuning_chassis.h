/**
 * @file    tuning_chassis.h
 * @brief   tuning 服务内部私有子模块：底盘速度环调参 profile。
 *
 * 仅供 tuning.c 编排调用，不是服务公共面。持有本 profile 的 VOFA 变量组
 * （cmd 输入 + tx 遥测副本），实现「安全重置 → 注册 → 应用 → 刷新」四步。
 *
 * 变量组内容（W1：增益外显，tx 6→10）：
 * - tx×10（通道序）：kp/ki/kd L、kp/ki/kd R（cmd 单向回显）、目标 L/R、反馈 L/R
 *   （目标/反馈来自 Chassis_Telemetry_T 快照；pwm 不再外显）；
 * - cmd×8：LM/RM（目标 m/s）、LP/LI/LD、RP/RI/RD（增益）——命令名沿用旧 profile。
 *   语义：kp/ki/kd 与目标既控制(cmd)又显示(tx 回显)；当前(反馈)只显示。
 */
#ifndef TUNING_CHASSIS_H
#define TUNING_CHASSIS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  进入本 profile：cmd/tx 组重置安全值（增益 0、目标 0）→ 注册 VOFA 变量组
 *         → Chassis_Stop（确定性起点）→ 立即应用安全 cmd（覆写底盘残留增益/目标）。
 * @note   调用者（tuning.c）负责先 vofa_clear_profile。
 */
void TuningChassis_Enter(void);

/** cmd 组 → Chassis 公共 API 单向应用（增益 ×2 + 目标）。每拍无条件调用。 */
void TuningChassis_Apply(void);

/** Chassis_GetTelemetry 快照 → tx 组单向复制（下一拍随帧发出）。 */
void TuningChassis_RefreshTx(void);

/** 推进内环：Chassis_Update（内环自门控 10ms）。 */
void TuningChassis_PumpInner(void);

/** 退出本 profile：Chassis_Stop（清 VOFA profile 归调用者）。 */
void TuningChassis_Exit(void);

#ifdef __cplusplus
}
#endif

#endif /* TUNING_CHASSIS_H */
