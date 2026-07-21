/**
 * @file    tuning_gimbal.h
 * @brief   tuning 服务内部私有子模块：云台位置环（vision_aim PD）静态调参 profile（W8 契约 §30）。
 *
 * 仅供 tuning.c 编排调用，不是服务公共面。持有本 profile 的 VOFA 变量组
 * （cmd 输入 + tx 遥测副本），实现「安全重置 → 注册 → 应用 → 刷新」，同 tuning_chassis 模式。
 *
 * 变量组内容：
 * - tx×13（通道序）：err_x err_y delta_x delta_y cur_x cur_y state
 *   + XP XD YP YD DB MS（cmd 清洗后回显）。err/delta/cur/state 来自 Gimbal_GetTelemetry
 *   快照单向副本；回显来自 Apply 存下的清洗后应用值（单一清洗点，RefreshTx 不复洗）。
 * - cmd×7：XP/XD/YP/YD（逐轴 kp/kd）、DB（双轴共享死区 px）、MS（双轴共享步长脉冲）、
 *   GO（≥0.5 边沿一次性消费 → Gimbal_ReselectTopic，应用后清 0）。
 *
 * 安全初值（Enter/重进一律执行）：增益全 0、DB=10000、MS=1、GO=0。
 * 关键事实：vision_aim floor-1 语义下 kp=0 **不是**零出力（|err|>死区时 raw=0 也走 ±1 爬行）；
 * 零出力由 DB=10000（整幅图像在死区内 → delta 恒 0）保证。
 *
 * 清洗（§7 外部输入边界，唯一清洗点=Apply）：kp/kd/DB 负值→0、MS<1→1、MS float→int32 截断。
 */
#ifndef TUNING_GIMBAL_H
#define TUNING_GIMBAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  进入本 profile：cmd/tx 组重置安全值 → 注册 VOFA 变量组 → Gimbal_Stop（确定性起点）
 *         → 立即应用安全 cmd（DB=10000 覆写云台残留调参，确定性零出力）。
 * @note   调用者（tuning.c）负责先 vofa_clear_profile；装配层（app_compose）负责在本函数
 *         **之后**再 Gimbal_SelectTopic（本函数内 Gimbal_Stop 会终止进行中的握手，契约 §30 顺序）。
 */
void TuningGimbal_Enter(void);

/** cmd 组 → 清洗 → Gimbal_SetAimTuning 单向应用；GO≥0.5 → 消费清 0 → Gimbal_ReselectTopic。 */
void TuningGimbal_Apply(void);

/** Gimbal_GetTelemetry 快照 + 清洗后应用值 → tx 组单向复制（下一拍随帧发出）。 */
void TuningGimbal_RefreshTx(void);

/** 推进内环：Gimbal_Update（内环自门控 10ms）。 */
void TuningGimbal_PumpInner(void);

/** 退出本 profile：Gimbal_Stop（清 VOFA profile 归调用者）。 */
void TuningGimbal_Exit(void);

#ifdef __cplusplus
}
#endif

#endif /* TUNING_GIMBAL_H */
