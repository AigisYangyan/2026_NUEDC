/**
 * @file    move_profile.h
 * @brief   定长运动速度剖面（Middleware 纯函数）——按距离参数化的梯形速度前馈
 *
 * 模块职责：
 * - 唯一能力：给定「已行进距离 + 目标总距离 + 加减速标定」，算出这一拍该给的前进前馈基速。
 *   剖面按**距离**参数化（加速-匀速-减速），减速段随剩余距离收敛到 0。
 *   成为「定长运动的速度剖面」这一数据变换的唯一所有者。
 *
 * 设计约定（AGENTS.md §3.3 / §8.2 / phase4 计划表 §27 契约）：
 * 1. 纯函数、**无状态**：剖面是 (dist_done, target, cfg) 的确定函数，每拍以实测距离重算——
 *    这正是它「自带位置反馈」的原因（区别于 speed_plan 的有状态斜坡）。无 malloc、无模块 static。
 *    不采样、不碰 Chassis/电机，不含任何 Driver/App/其他 Middleware 头（仅 <math.h>）。
 * 2. 与 speed_plan 划清边界：speed_plan 是「横向误差幅值→巡航基速有界斜坡」（曲率代理）；
 *    本模块是「剩余距离→加减速前馈」（定长运动），输入域不同，非复刻、非竞争所有者。
 * 3. 距离→速度换算内部把 mm→m 仅为量纲对齐，**不构成第二个脉冲→距离换算所有者**
 *    （脉冲→mm 唯一属 odometry cfg；已行进距离 dist 唯一属调用者 motion.c）。
 * 4. 输出自持限幅在 [0, cruise_mps]：前馈永不超过匀速上限，天然回避
 *    Chassis_SetTargetMps 无目标限幅的已知空缺，不指望底盘兜底。
 * 5. 无纵向 PID：剖面即位置闭环，不新增第二个从距离信号算速度的所有者（§8.2）。
 *    近终点减速到 0 附近若因静摩擦失速属已知标定项——交调用者超时/现场标定，本模块不加驱动下限
 *    （主机无静摩擦不可验证，同 motion TURN 先例）。
 * 6. 标定量（cruise/start/accel/decel）由调用者提供，无默认值。
 */
#ifndef HC_TEAM_MIDDLEWARE_MOVE_PROFILE_MOVE_PROFILE_H
#define HC_TEAM_MIDDLEWARE_MOVE_PROFILE_MOVE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

/** 梯形剖面配置。全部是标定事实，由调用者提供，无默认值。 */
typedef struct {
    float cruise_mps;   /* 匀速段速度上限 v_cruise（m/s），> 0 */
    float start_mps;    /* 加速段起始速度（脱离静摩擦的起步速，m/s），0 <= start <= cruise */
    float accel_mps2;   /* 加速度 a_acc（m/s^2），> 0 */
    float decel_mps2;   /* 减速度 a_dec（m/s^2），> 0 */
} MoveProfile_Config_T;

/**
 * @brief  返回本拍前进前馈基速（m/s）。
 * @param  cfg           剖面配置；cfg==NULL → 返回 0（调用者误用，同 pid/odometry 口径不断言）。
 * @param  dist_done_mm  已行进前进距离（mm，由调用者从 odometry 位姿自算，< 0 视为 0）。
 * @param  target_mm     目标总距离（mm）；<= 0 → 返回 0（无效目标）。
 * @return v = clamp( min( cruise_mps,
 *                         sqrt(start_mps^2 + 2*accel_mps2*s_m),   // 加速段（起步速兜底）
 *                         sqrt(2*decel_mps2*rem_m) ),             // 减速段 → 0
 *                    0, cruise_mps )
 *         其中 s_m = dist_done_mm/1000、rem_m = (target_mm - dist_done_mm)/1000。
 * @note   dist_done_mm >= target_mm → 返回 0（到位/越界，由调用者负责发 Chassis_Stop）。
 *         纯函数、无副作用、无状态。
 */
float MoveProfile_Speed(const MoveProfile_Config_T *cfg,
                        float dist_done_mm, float target_mm);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_MIDDLEWARE_MOVE_PROFILE_MOVE_PROFILE_H */
