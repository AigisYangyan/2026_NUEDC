/**
 * @file    imu_check.h
 * @brief   IMU 链路诊断服务：航向/角速度/新鲜度/链路健康遥测 + 静置漂移测量。
 *
 * 能力辩护（§3.4）：IMU 器件能报航向、角速度、数据新鲜度与链路健康计数，
 * 能测静置漂移——仅此成为公共面。只读诊断，不发电机命令，不写器件配置。
 *
 * 所有权（§8.2）：航向/角速度解析、校验、新鲜度全归 driver/imu；本服务只做
 * 快照/诊断的单向镜像 + 漂移统计（新数据变换：基准角差 ÷ 经过时间，唯一所有者
 * 是本服务）。刻意不调 Imu_ZeroYaw/Imu_SetOutputRate——二者写器件内部 flash 且
 * 阻塞约 200ms，禁入周期任务；漂移用软件基准角，进条目即测，零器件磨损。
 *
 * 泵所有权（V23 补注）：本服务是 Imu_Update() 的第二个泵点（第一 = motion 激活期）。
 * 缓解 = scheduler 单活动条目互斥（EncoderTest/GrayTest 对 V21 的同款先例）。
 */
#ifndef IMU_CHECK_H
#define IMU_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 一次读取到的诊断遥测（与 VOFA 通道 ch0..ch7 同源同序）。 */
typedef struct {
    float    yaw_deg;         /**< 镜像 Imu_Snapshot_t::yaw_deg，零第二处理。 */
    float    yaw_rate_dps;    /**< 镜像 Imu_Snapshot_t::yaw_rate_dps。 */
    uint32_t age_ms;          /**< 镜像 Imu_Snapshot_t::age_ms。 */
    bool     valid;           /**< 镜像 Imu_Snapshot_t::valid。 */
    float    drift_dps;       /**< 静置漂移：wrap 归一化的（yaw − 基准角）/ 经过秒。
                                   基准在首个 valid 到期拍播种；经过 < 2s 时恒 0
                                   （短窗斜率是噪声，量级纪律）。 */
    uint32_t frame_count;     /**< 镜像 Imu_Diag_t::frame_count。 */
    uint32_t checksum_errors; /**< 镜像 Imu_Diag_t::checksum_error_count。 */
    uint32_t rx_overflows;    /**< 镜像 Imu_Diag_t::rx_overflow_count。 */
} ImuCheck_Telemetry_T;

/**
 * @brief 进入诊断会话：清 VOFA 表 + 注册 tx×8 + 镜像/漂移基准复位。
 * @note  不发电机命令、不向器件发送任何字节。调用前置：vofa_init() 已执行。
 */
void ImuCheck_Start(void);

/**
 * @brief 周期推进：10ms 自门控——Imu_Update 排空 → 快照/诊断镜像 → 漂移统计 → vofa_run。
 * @param now_ms 调度器注入的当前毫秒时刻（无符号减法天然处理回绕）。
 * @note  首拍只播种周期基准，不采样不发帧。
 */
void ImuCheck_Update(uint32_t now_ms);

/** @brief 退出诊断会话：清 VOFA 表。无硬件副作用。 */
void ImuCheck_Stop(void);

/**
 * @brief 读取最近一次到期拍的遥测。
 * @param out 输出；为 NULL 时函数无副作用直接返回。
 */
void ImuCheck_GetTelemetry(ImuCheck_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* IMU_CHECK_H */
