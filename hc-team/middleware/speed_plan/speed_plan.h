/**
 * @file    speed_plan.h
 * @brief   速度规划器（Middleware）——横向误差幅值 → 有状态有界斜坡巡航基速
 *
 * 模块职责：
 * - 唯一能力：把逐拍传入的横向误差幅值 |error_mm|（曲率代理）映射为巡航基速目标，
 *   并对调用者持有的当前基速做有界斜坡（直道→朝 straight 提速、入弯→朝 min 降速），
 *   输出建议基速（m/s）。成为「基速调制」这一数据变换的唯一所有者。
 *
 * 设计约定（AGENTS.md §3.3 / §8.2 / phase4 计划表 §17 契约）：
 * 1. 纯算法、状态由调用者显式持有（SpeedPlan_T 上下文，无 malloc、无模块 static）。
 *    不采样、不量化误差、不碰 Chassis/电机、不含任何 Driver/App/其他 Middleware 头。
 * 2. 误差量化唯一所有者是 track_error：调用者取其已算好的 error_mm、fabsf 后按值喂入，
 *    本模块绝不复算误差、绝不新开采样点（同 track_elements 的并列消费方式）。
 * 3. 输出基速自持限幅在 [min_speed, straight_speed]（拓扑登记「Chassis 无目标限幅」空缺——
 *    限速上/下限落在本模块配置，同 diff_limit_mps 先例，不指望 Chassis 兜底）。
 *    差速修正与差速限幅仍归外环 PID，本模块零触碰。
 * 4. 无反向 / 无滤波 / 无积分 / 无微分——纯有界斜坡；曲率来源是误差幅值，非重新量化。
 * 5. 阈值（straight/min/curve_error/accel/decel）是标定事实，由调用者提供，无默认值。
 */
#ifndef HC_TEAM_MIDDLEWARE_SPEED_PLAN_SPEED_PLAN_H
#define HC_TEAM_MIDDLEWARE_SPEED_PLAN_SPEED_PLAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 速度规划配置。全部是标定事实，由调用者提供，无默认值。 */
typedef struct {
    float straight_speed_mps;   /* 直道巡航基速（|error|≈0 时的目标上限），> 0 */
    float min_speed_mps;        /* 入弯最低基速（|error|≥curve_error_mm 时的目标下限），0 ≤ min ≤ straight */
    float curve_error_mm;       /* 达到 min_speed 的误差幅值阈（|error|≥此值→min_speed），> 0；
                                   ≤ 0 视为退化配置：不做曲率降速，目标恒为 straight_speed */
    float accel_mps_per_s;      /* 提速斜坡速率上限（m/s per second），> 0 */
    float decel_mps_per_s;      /* 降速斜坡速率上限（m/s per second），> 0 */
} SpeedPlan_Config_T;

/** 规划器上下文。**调用者分配**（栈或静态皆可，无 malloc）。current_mps 为私有斜坡状态。 */
typedef struct {
    SpeedPlan_Config_T cfg;
    float current_mps;          /* 当前规划基速（斜坡状态），恒被约束在 [min, straight] */
} SpeedPlan_T;

/**
 * @brief  初始化 / 复位规划器：存配置，current_mps 复位为 min_speed_mps（安全起步）。
 * @param  sp   上下文，调用者持有。sp==NULL 或 cfg==NULL 时静默无副作用返回吸收
 *              （同 pid/track_elements/odometry 口径：NULL 视为调用者误用，不做断言/错误码）。
 * @param  cfg  规划配置，按值拷入 sp->cfg。
 */
void  SpeedPlan_Init(SpeedPlan_T *sp, const SpeedPlan_Config_T *cfg);

/**
 * @brief  推进一拍并返回新的规划基速（m/s）。
 * @param  sp            上下文，sp==NULL → 返回 0，无副作用。
 * @param  abs_error_mm  横向误差幅值（内部再取 fabsf 防调用者误传符号误差）。
 * @param  elapsed_ms    距上一拍的真实毫秒数（调用者门控 elapsed，与底盘同源）。
 * @note   目标映射：curve_error_mm≤0 → straight；否则 straight+min(|e|/curve_error,1)×(min−straight)，
 *         夹到 [min,straight]。斜坡：Δcap=(target>current?accel:decel)×elapsed_ms/1000，
 *         |target−current|≤Δcap 直接到位、否则朝 target 步进 Δcap（elapsed_ms==0 → 不变）。
 */
float SpeedPlan_Update(SpeedPlan_T *sp, float abs_error_mm, uint32_t elapsed_ms);

/** 当前规划基速。sp==NULL → 0。 */
float SpeedPlan_GetSpeed(const SpeedPlan_T *sp);

/** current_mps 回 min_speed（重新起步，不改 cfg）。sp==NULL 吸收。 */
void  SpeedPlan_Reset(SpeedPlan_T *sp);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_MIDDLEWARE_SPEED_PLAN_SPEED_PLAN_H */
