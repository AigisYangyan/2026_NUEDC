/**
 * @file    heading.h
 * @brief   航向角去卷（unwrap）——[-180,180) 有界角 → 连续多圈角（Middleware）
 *
 * 模块职责：
 * - 唯一能力：把 IMU 输出的有界航向角 [-180,180) 累积成连续的多圈航向角。
 *   这是 imu.h:12 明示「[-180,180) 到连续多圈的转换属 Middleware/Service」的落点，
 *   也是本工程 yaw unwrap 的**唯一所有者**。
 *
 * 设计约定（AGENTS.md §3.3 / §8.2）：
 * 1. 纯算法：不读传感器（yaw 由调用者按值传入，Middleware 不含 Driver 头）。
 * 2. unwrap ≠ 滤波/积分：器件内部已 Kalman 解算并积分（imu.h:9-10），本模块只做无损的
 *    「跨界补 ±360」拓扑提升，不得再对 yaw 做第二次滤波或积分。
 * 3. 方向符号修正不在本模块（imu.h:11 要求单点实测定值）——见 odometry.h `heading_sign`。
 * 4. 采样 Nyquist 假设：|连续有效样本间 yaw 变化| < 180°。IMU 200/500 Hz 输出下任何现实
 *    yaw rate 都满足；器件掉线 gap 期若转过 >180° 会误计一圈（dead-reckoning 固有局限）。
 */
#ifndef HC_TEAM_MIDDLEWARE_ODOMETRY_HEADING_H
#define HC_TEAM_MIDDLEWARE_ODOMETRY_HEADING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 去卷上下文。定义暴露仅为让调用者静态分配（或内嵌进 Odometry_T）；
 * 全部字段为模块私有运行时状态，调用者不得直接读写。
 */
typedef struct {
    float   last_wrapped_deg;   /* 上一有效样本（[-180,180) 原值） */
    int32_t wrap_count;         /* 累计跨界圈数（CCW 连续 → 递增） */
    bool    seeded;             /* 是否已收到首样本 */
} Heading_T;

/** @brief 复位去卷状态：圈数清零、seeded=false。ctx==NULL 无副作用。 */
void Heading_Reset(Heading_T *ctx);

/**
 * @brief  送入一个有界 yaw 样本，返回连续多圈航向角。
 * @param  ctx              调用者持有的上下文。
 * @param  yaw_wrapped_deg  IMU 输出的有界航向角，单位度，口径 [-180,180)。
 * @return 连续航向角（度）：首样本原值返回；此后为 yaw + wrap_count×360。
 * @note   ctx==NULL 时直接返回 yaw_wrapped_deg（无副作用）。
 *         跨界判据：delta = yaw − 上一样本；delta < −180 → wrap_count++（越 −180 向正）；
 *         delta > 180 → wrap_count−−（越 +180 向负）。见文件头 Nyquist 假设。
 */
float Heading_Unwrap(Heading_T *ctx, float yaw_wrapped_deg);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_MIDDLEWARE_ODOMETRY_HEADING_H */
