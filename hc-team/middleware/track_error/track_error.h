/**
 * @file    track_error.h
 * @brief   循迹误差估计器（Middleware）——12 路灰度深色位图的加权重心量化
 *
 * 模块职责：
 * - 唯一能力：把深色位图量化为带符号横向误差（加权重心，物理单位 mm）。
 *
 * 设计约定（AGENTS.md §3.3 / phase3 计划表 §6 契约）：
 * 1. 纯算法、无状态：不读传感器（位图由调用者按值传入，Middleware 不含 Driver 头）、
 *    不做丢线回退/记忆（控制策略归 Service）、不做赛道特征识别（归调用者）。
 * 2. 坐标系：探头坐标 = (index − 5.5) × pitch_mm，index 按「车左→车右」（驾驶员视角）。
 *    **+误差 = 线在车中心右侧**。|误差| ≤ 5.5 × pitch_mm 天然成立。
 * 3. `bit0_is_left` 是位序左右的**唯一**修正点（driver/gray/gray.h 位序警告的落点，
 *    厂商 P1=最右与 syscfg 注释矛盾，待硬件 H2 实测定值）。禁止在别处再加反转。
 * 4. `pitch_mm` 是机械安装事实（迹系列探头间距用户自定义，手册 p12-13），无默认值。
 */
#ifndef HC_TEAM_MIDDLEWARE_TRACK_ERROR_TRACK_ERROR_H
#define HC_TEAM_MIDDLEWARE_TRACK_ERROR_TRACK_ERROR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 阵列路数。Middleware 不含 Driver 头，自持常数；与 Driver 的一致性由 Service 负责。 */
#define TRACK_ERROR_CHANNEL_COUNT 12u

/** 量化配置。两个字段都是安装事实，由调用者提供。 */
typedef struct
{
    float pitch_mm;      /* 相邻探头中心间距，>0（机械安装自定义，无默认值） */
    bool  bit0_is_left;  /* bit0(=PIN_IN1) 是否位于车左（驾驶员视角）；位序唯一修正点 */
} TrackError_Config_T;

/**
 * @brief  加权重心量化：深色位图 → 带符号横向误差。
 *
 * @param config        量化配置，必须有效且 pitch_mm > 0。
 * @param dark_bitmap   深色位图，仅低 TRACK_ERROR_CHANNEL_COUNT 位有效（高位内部屏蔽）；
 *                      bit=1 表示该路压在深色（黑线）上。
 * @param out_error_mm  误差输出存储，必须有效。仅在返回 true 时写入。
 * @return true = 有线（至少一路置位），*out_error_mm 为置位通道坐标的算术平均；
 *         false = 丢线（有效位全 0），不写 *out_error_mm。
 */
bool TrackError_FromDarkBitmap(const TrackError_Config_T *config,
                               uint16_t dark_bitmap,
                               float *out_error_mm);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_MIDDLEWARE_TRACK_ERROR_TRACK_ERROR_H */
