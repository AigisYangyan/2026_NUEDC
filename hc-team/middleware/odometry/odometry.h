/**
 * @file    odometry.h
 * @brief   里程计位姿 dead-reckoning（Middleware）——编码器 Δ + IMU 航向 → x,y,θ
 *
 * 模块职责：
 * - 唯一能力：把「一拍左右轮增量脉冲 + 一拍 IMU 有界航向」积分成底盘平面位姿
 *   （连续航向角 heading_deg 与位置 x_mm,y_mm）。
 * - 航向权威 = IMU 去卷航向（内嵌 Heading_T）；前进距离 = 双轮增量均值 × mm_per_pulse，
 *   沿该航向积分位置。不用轮差分航向（抗轮滑、免 track_width 无实测机械常数）。
 *
 * 设计约定（AGENTS.md §3.3 / §8.2 / M01 契约 §14）：
 * 1. 纯算法：不读传感器（增量与 yaw 按值传入，Middleware 不含 encoder.h/imu.h）；
 *    不采样、不推进 Encoder（采样与 elapsed 所有权归 chassis.c，M01 仅按值消费快照字段）。
 * 2. 单一所有者：编码器方向反转归 encoder.c s_direction_sign（收到已修正 delta，不复反转）；
 *    IMU yaw 符号修正**唯一点** = cfg.heading_sign（imu.h:11 要求实测单点）；
 *    脉冲→距离换算**唯一点** = cfg.mm_per_pulse（新变换，不碰 speed_mps 速度链）；
 *    yaw unwrap 唯一点 = heading.c。
 * 3. 空间积分：位姿按前进距离×航向累加，无时间门控，不需 elapsed_ms。
 * 4. Odometry_T 的 cfg 之外字段全部私有，调用者不得直接读写；唯一读出口 Odometry_GetPose()。
 */
#ifndef HC_TEAM_MIDDLEWARE_ODOMETRY_ODOMETRY_H
#define HC_TEAM_MIDDLEWARE_ODOMETRY_ODOMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "middleware/odometry/heading.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 里程计配置。两个字段都是标定事实，由调用者提供。 */
typedef struct {
    float mm_per_pulse;   /* 单脉冲对应轮面前进距离（mm/脉冲），>0，机械标定，无默认值 */
    float heading_sign;   /* IMU yaw → 位姿航向的符号修正，+1 或 −1；唯一修正点，默认 +1 不预设 */
} Odometry_Config_T;

/** 位姿快照（对外只读输出）。 */
typedef struct {
    float x_mm;         /* 起点系 X（首帧起点为原点，+X = 起始航向 0° 方向） */
    float y_mm;         /* 起点系 Y */
    float heading_deg;  /* 连续多圈航向角（度） */
} Odometry_Pose_T;

/**
 * 里程计上下文。定义暴露仅为让调用者静态分配；cfg 之外字段全部私有运行时状态。
 */
typedef struct {
    Odometry_Config_T cfg;

    /* -- 私有运行时状态，调用者不得触碰 ------------------------------------ */
    Heading_T heading;      /* 内嵌去卷状态 */
    float     x_mm;
    float     y_mm;
    float     heading_deg;  /* 最近一次 = 去卷航向 × heading_sign；无有效样本前为 0 */
} Odometry_T;

/**
 * @brief 初始化上下文：按值拷入配置、清零位姿、复位去卷状态。
 * @param ctx    调用者持有的上下文。
 * @param cfg    初始配置；cfg==NULL 时配置归零（mm_per_pulse=0 会使位移恒为 0，属误用）。
 * @note  ctx==NULL 无副作用。前置条件：cfg.mm_per_pulse > 0，heading_sign ∈ {+1,−1}
 *        （由调用者保证，本模块不做运行期拒绝，同 pid/track_error 契约口径）。
 */
void Odometry_Init(Odometry_T *ctx, const Odometry_Config_T *cfg);

/** @brief 清零位姿与去卷状态，保留配置。用于一次运行开始前复位到原点。ctx==NULL 无副作用。 */
void Odometry_Reset(Odometry_T *ctx);

/**
 * @brief 推进一拍位姿。
 * @param ctx                 上下文。
 * @param delta_left_pulses   本拍左轮增量脉冲（已由 encoder.c 完成方向修正，有符号）。
 * @param delta_right_pulses  本拍右轮增量脉冲（同上）。
 * @param yaw_wrapped_deg     本拍 IMU 有界航向角，[-180,180) 度。
 * @param heading_valid       IMU 本拍快照是否有效（调用者据 Imu_Snapshot_t::valid/age_ms 判定）。
 * @note  heading_valid=true → 更新航向 = Heading_Unwrap(yaw) × cfg.heading_sign；
 *        heading_valid=false → 保持上次航向（不推进去卷状态），仍沿该航向积分前进距离。
 *        前进距离 = (delta_left + delta_right) × 0.5 × mm_per_pulse。ctx==NULL 无副作用。
 */
void Odometry_Update(Odometry_T *ctx,
                     int32_t delta_left_pulses,
                     int32_t delta_right_pulses,
                     float yaw_wrapped_deg,
                     bool heading_valid);

/** @brief 读出当前位姿（只读，不改状态）。ctx==NULL 或 out==NULL 无副作用。 */
void Odometry_GetPose(const Odometry_T *ctx, Odometry_Pose_T *out);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_MIDDLEWARE_ODOMETRY_ODOMETRY_H */
