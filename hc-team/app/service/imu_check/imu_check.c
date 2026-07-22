/**
 * @file    imu_check.c
 * @brief   IMU 链路诊断服务实现：VOFA tx×8 只读遥测 + 10ms 自门控 + 静置漂移统计。
 *
 * 数据链（§8.2）：
 *   ImuUart FIFO → Imu_Update [imu.c：解析/校验/新鲜度] → Imu_GetSnapshot / Imu_GetDiag
 *   → 本服务 tx 镜像（单向复制，零第二处理）+ 漂移统计（本服务唯一新变换：
 *   wrap 归一化的基准角差 ÷ 经过秒）→ vofa_run [uart_vofa：JustFloat 组帧发送]。
 *
 * 所有权：解析/校验/量程/新鲜度唯一在 imu.c；漂移统计唯一在本服务。
 * 不调 Imu_ZeroYaw/Imu_SetOutputRate（写器件 flash + 阻塞 ~200ms，禁入周期任务）。
 */
#include "app/service/imu_check/imu_check.h"

#include "driver/imu/imu.h"
#include "driver/uart_vofa/uart_vofa.h"

#define IMU_CHECK_STREAM_PERIOD_MS 10u
/* 漂移最短统计窗：短窗斜率被快照量化/噪声支配（量级纪律），2s 起报。 */
#define IMU_CHECK_DRIFT_MIN_MS     2000u

/* tx 遥测镜像（注册序 = 上位机通道序 ch0..ch7）：
 * yaw/rate/drift 为 float；age/valid/三计数经 register_int→JustFloat 转 float
 * 发送（|值|<2^24 精确；计数一次会话内远低于此）。 */
static float s_tx_yaw_deg;
static float s_tx_yaw_rate_dps;
static int   s_tx_age_ms;
static int   s_tx_valid;
static float s_tx_drift_dps;
static int   s_tx_frame_count;
static int   s_tx_checksum_errors;
static int   s_tx_rx_overflows;

static bool     s_seeded;
static uint32_t s_period_base_ms;

static bool     s_drift_seeded;
static float    s_drift_ref_yaw_deg;
static uint32_t s_drift_ref_ms;

/** 角差归一化到 [-180, 180)：跨 ±180 界的漂移不得放大成一整圈。 */
static float imu_check_normalize_deg(float deg)
{
    while (deg >= 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

static void imu_check_refresh_tx(uint32_t now_ms)
{
    Imu_Snapshot_t snap;
    Imu_Diag_t diag;

    Imu_GetSnapshot(&snap);
    Imu_GetDiag(&diag);

    /* 单向复制，零第二处理（§8.2 单一所有者）。 */
    s_tx_yaw_deg         = snap.yaw_deg;
    s_tx_yaw_rate_dps    = snap.yaw_rate_dps;
    s_tx_age_ms          = (int)snap.age_ms;
    s_tx_valid           = snap.valid ? 1 : 0;
    s_tx_frame_count     = (int)diag.frame_count;
    s_tx_checksum_errors = (int)diag.checksum_error_count;
    s_tx_rx_overflows    = (int)diag.rx_overflow_count;

    /* 漂移统计（本服务唯一新变换）：首个 valid 到期拍播种基准，窗满起报。 */
    if (snap.valid) {
        if (!s_drift_seeded) {
            s_drift_seeded = true;
            s_drift_ref_yaw_deg = snap.yaw_deg;
            s_drift_ref_ms = now_ms;
            s_tx_drift_dps = 0.0f;
        } else {
            uint32_t elapsed_ms = now_ms - s_drift_ref_ms;
            if (elapsed_ms >= IMU_CHECK_DRIFT_MIN_MS) {
                float delta = imu_check_normalize_deg(snap.yaw_deg - s_drift_ref_yaw_deg);
                s_tx_drift_dps = delta / ((float)elapsed_ms / 1000.0f);
            } else {
                s_tx_drift_dps = 0.0f;
            }
        }
    } else {
        s_tx_drift_dps = 0.0f;
    }
}

void ImuCheck_Start(void)
{
    vofa_clear_profile();

    s_tx_yaw_deg         = 0.0f;
    s_tx_yaw_rate_dps    = 0.0f;
    s_tx_age_ms          = 0;
    s_tx_valid           = 0;
    s_tx_drift_dps       = 0.0f;
    s_tx_frame_count     = 0;
    s_tx_checksum_errors = 0;
    s_tx_rx_overflows    = 0;

    /* 注册顺序 = 通道序 ch0..ch7；零 bind_cmd（只读诊断，不接收上位机命令）。 */
    (void)vofa_register_float(&s_tx_yaw_deg);
    (void)vofa_register_float(&s_tx_yaw_rate_dps);
    (void)vofa_register_int(&s_tx_age_ms);
    (void)vofa_register_int(&s_tx_valid);
    (void)vofa_register_float(&s_tx_drift_dps);
    (void)vofa_register_int(&s_tx_frame_count);
    (void)vofa_register_int(&s_tx_checksum_errors);
    (void)vofa_register_int(&s_tx_rx_overflows);

    s_seeded = false;
    s_period_base_ms = 0u;
    s_drift_seeded = false;
    s_drift_ref_yaw_deg = 0.0f;
    s_drift_ref_ms = 0u;
}

void ImuCheck_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if (!s_seeded) {
        /* 首拍无历史时刻可算 elapsed：只播种采样基准。 */
        s_seeded = true;
        s_period_base_ms = now_ms;
        return;
    }

    elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */
    if (elapsed_ms < IMU_CHECK_STREAM_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    /* 第二 Imu_Update 泵点（V23 补注，单活动条目互斥）：排空 FIFO 防溢出。 */
    Imu_Update();
    imu_check_refresh_tx(now_ms);
    vofa_run(); /* RX 排空（零绑定 = 无副作用）+ 发本拍刚刷新的遥测帧 */
}

void ImuCheck_Stop(void)
{
    vofa_clear_profile();
}

void ImuCheck_GetTelemetry(ImuCheck_Telemetry_T *out)
{
    if (out == (void *)0) {
        return;
    }
    out->yaw_deg         = s_tx_yaw_deg;
    out->yaw_rate_dps    = s_tx_yaw_rate_dps;
    out->age_ms          = (uint32_t)s_tx_age_ms;
    out->valid           = (s_tx_valid != 0);
    out->drift_dps       = s_tx_drift_dps;
    out->frame_count     = (uint32_t)s_tx_frame_count;
    out->checksum_errors = (uint32_t)s_tx_checksum_errors;
    out->rx_overflows    = (uint32_t)s_tx_rx_overflows;
}
