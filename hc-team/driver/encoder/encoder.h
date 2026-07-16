/**
 * @file    encoder.h
 * @brief   两轮差速底盘编码器模块对外接口
 *
 * @details
 * 模块职责：
 * - 初始化编码器采样基准。
 * - 周期读取左右轮编码器总数、计算脉冲增量并换算线速度（m/s）。
 * - 以按值快照形式向 Service 暴露数据，不写入其他 Driver 的全局状态。
 *
 * 单位与口径约定：
 * - 累计脉冲、增量脉冲：有符号整数。
 * - 速度：米/秒（m/s）。
 * - 调用者必须传入真实 elapsed_ms；Encoder 不再维护固定采样周期全局参数。
 *
 * 依赖：
 * - 底层原始计数快照：`BoardGpio_GetEncoderRawSnapshot()`（Driver 层）。
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 编码器轮索引。 */
typedef enum {
    ENCODER_LEFT = 0,
    ENCODER_RIGHT,
    ENCODER_COUNT
} Encoder_Id;

/** 左右轮在同一采样时刻的一致快照。 */
typedef struct {
    int32_t total_pulses[ENCODER_COUNT];
    int32_t delta_pulses[ENCODER_COUNT];
    float   speed_mps[ENCODER_COUNT];
} Encoder_Snapshot;

/** 初始化编码器模块，建立初始计数基线。首拍 delta/speed 必须为零。 */
void Encoder_Init(void);

/**
 * @brief 使用调用者提供的真实 elapsed_ms 更新快照。
 * @param elapsed_ms 距离上次更新的毫秒数；必须大于 0。
 * @return true 更新成功；false 参数非法或无法读取原始计数。
 * @note 累计计数差值使用无符号模运算处理 int32_t 回绕。
 */
bool Encoder_Update(uint32_t elapsed_ms);

/**
 * @brief 在一个短临界区复制完整快照。
 * @param out 调用者拥有的输出结构；必须非空。
 */
void Encoder_GetSnapshot(Encoder_Snapshot *out);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_H */
