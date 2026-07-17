/**
 * @file    imu.h
 * @brief   单轴姿态模组 Driver：Z 轴航向角与角速度读取。
 *
 * 器件为串口输出的单轴 IMU，内部已完成 Kalman 解算，直接输出解算后的航向角
 * 与 Z 轴角速度。本 Driver 只负责「协议解析 + 快照 + 新鲜度」。
 *
 * 本 Driver 刻意不做的事（AGENTS.md §8.2 单一所有者）：
 * - 不做航向角积分 —— 器件内部已积分，再积一次就是重复处理。
 * - 不做角速度滤波 —— 器件内部已滤波。
 * - 不做方向反转 —— 安装方向的符号修正只能有一个所有者，且必须实测后再定。
 * - 不做航向角 unwrap —— [-180,180) 到连续多圈的转换是数据处理，属 Middleware/Service。
 *
 * 调用上下文：Imu_Update() 与 Imu_GetSnapshot() 均为任务态。ISR 只把字节搬进
 * ImuUart 私有 FIFO，不解析。二者同为任务态，故快照无需临界区保护。
 */
#ifndef IMU_H
#define IMU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 模组自主输出速率。
 * @note  枚举值与器件寄存器编码无关，编码映射隐藏在 imu.c 内部。
 * @note  **只增不改，新档一律追加在末尾** —— 插入中间会让既有取值静默映射到
 *        别的寄存器编码上。
 * @note  输出速率**不影响精度**：器件内部采样 50 kHz、Kalman 常驻，分辨率与航向
 *        精度在任何档位下相同。本项只改变数据新鲜度（最坏数据龄 = 1/速率）。
 *        器件未提供 1000 Hz 档的必要性：其 0.18° 陈旧误差已低于器件自身 0.2°
 *        航向精度，属追噪声，故不暴露。
 */
typedef enum {
    IMU_OUTPUT_RATE_10_HZ = 0, /**< 器件出厂默认；对底盘偏航闭环偏慢。 */
    IMU_OUTPUT_RATE_50_HZ,
    IMU_OUTPUT_RATE_100_HZ,
    IMU_OUTPUT_RATE_200_HZ,
    IMU_OUTPUT_RATE_500_HZ, /**< 云台前馈用：最坏数据龄 2 ms。需 230400 波特率。 */
} Imu_OutputRate_t;

/** @brief 一次读取到的姿态快照。 */
typedef struct {
    /** 航向角，单位度，范围 [-180, 180)。器件已解算，零点由 Imu_ZeroYaw() 设定。 */
    float yaw_deg;
    /** Z 轴角速度，单位度/秒。正方向取决于模组贴装方向，须上板实测确认。 */
    float yaw_rate_dps;
    /** 距最近一帧有效数据的毫秒数；valid == false 时恒为 0。 */
    uint32_t age_ms;
    /** 上电后是否已收到过至少一帧通过校验的数据。 */
    bool valid;
} Imu_Snapshot_t;

/** @brief 通信诊断计数，用于上板确认波特率与实测输出速率。 */
typedef struct {
    /** 累计通过校验的帧数。1 秒内增量 ÷ 2 即为实测输出速率（每周期两种帧）。 */
    uint32_t frame_count;
    /** 帧头与类型均正确但校验和不符的帧数。波特率错配时会显著增长。 */
    uint32_t checksum_error_count;
    /** 端口 RX FIFO 溢出而丢弃的字节数。持续增长说明 Imu_Update() 调用过疏。 */
    uint32_t rx_overflow_count;
} Imu_Diag_t;

/**
 * @brief 重置解析状态、快照与诊断计数。
 * @note  只复位本模块私有状态，不与器件通信。须在 ImuUart_Init() 之后、
 *        全局中断开启之前调用一次。
 */
void Imu_Init(void);

/**
 * @brief 排空端口 RX FIFO 并解析其中的完整帧，更新快照。
 * @note  任务态调用。必须周期调用，否则 FIFO 溢出（见 Imu_Diag_t::rx_overflow_count）。
 *        200 Hz 输出速率下端口 FIFO 可容纳约 64 ms 的数据。
 */
void Imu_Update(void);

/**
 * @brief 读取当前姿态快照。
 * @param out 输出；为 NULL 时函数无副作用直接返回。
 * @note  快照可能陈旧。周期控制必须检查 valid 与 age_ms —— 器件掉线时
 *        yaw_deg 会冻结在最后一帧，闭环会据此持续朝一个方向纠偏。
 */
void Imu_GetSnapshot(Imu_Snapshot_t *out);

/**
 * @brief 读取通信诊断计数。
 * @param out 输出；为 NULL 时函数无副作用直接返回。
 */
void Imu_GetDiag(Imu_Diag_t *out);

/**
 * @brief 将当前朝向设为航向角零点。
 * @return 三条指令是否都成功写入发送端口。
 * @note  阻塞约 200 ms（器件要求指令间延时）。**写器件内部 flash，掉电保存。**
 *        禁止在周期任务中调用；由上层在一次运行开始前调用一次。
 */
bool Imu_ZeroYaw(void);

/**
 * @brief 设置模组自主输出速率。
 * @param rate 目标速率；超出枚举范围时返回 false 且不发送任何字节。
 * @return 三条指令是否都成功写入发送端口。
 * @note  阻塞约 200 ms。**写器件内部 flash，掉电保存** —— 是一次性配置动作，
 *        不需要每次上电重发。禁止在周期任务中调用。
 */
bool Imu_SetOutputRate(Imu_OutputRate_t rate);

#ifdef __cplusplus
}
#endif

#endif /* IMU_H */
