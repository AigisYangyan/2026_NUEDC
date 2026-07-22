/**
 * @file    test_imu_check.c
 * @brief   IMU 链路诊断服务主机测试（W10，契约 §32）。
 *
 * 链接组成：真实 imu_check.c + imu.c + uart_vofa.c + board_uart×4
 *           + fake_uart_port.c（IMU RX 注入 + VOFA TX 抓取）。
 * imu.c 直读 Clock_NowMs / Mspm0Runtime_DelayMs（新鲜度/指令延时），本文件提供
 * 可控假实现（test_imu.c 同款）；imu_check 自身取 now_ms 参数注入，不直读 Clock。
 */
#include "app/service/imu_check/imu_check.h"

#include "driver/imu/imu.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* fake 注入/抓取接口 */
extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_PushImuBytes(const uint8_t *data, uint32_t length);
extern uint32_t FakeUartPort_CopyImuTxLog(uint8_t *out, uint32_t capacity);
extern void FakeUartPort_CompleteVofaTx(void);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

/* ---- imu.c 的主机侧时间/延时源（test_imu.c 同款）------------------------- */

static uint32_t s_fake_now_ms = 0u;

uint32_t Clock_NowMs(void)
{
    return s_fake_now_ms;
}

void Mspm0Runtime_DelayMs(uint32_t delay_ms)
{
    s_fake_now_ms += delay_ms;
}

/* ---- 断言 ---------------------------------------------------------------- */

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_FLOAT_NEAR(actual, expected, epsilon) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (double)(actual), (double)(expected), (double)(epsilon), \
               __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* ---- 夹具 ---------------------------------------------------------------- */

#define IMUC_FRAME_CHANNELS 8u
#define IMUC_FRAME_BYTES (IMUC_FRAME_CHANNELS * 4u + 4u) /* 8 通道 + JustFloat 帧尾 = 36 */

#define RX_HEAD      0x5Au
#define RX_TYPE_GYRO 0xAAu
#define RX_TYPE_YAW  0xBBu

/** 组一个校验和正确的读帧并推入 IMU 端口 FIFO。 */
static void push_frame(uint8_t type, int16_t raw)
{
    uint8_t frame[5];

    frame[0] = RX_HEAD;
    frame[1] = type;
    frame[2] = (uint8_t)((uint16_t)raw & 0xFFu);
    frame[3] = (uint8_t)(((uint16_t)raw >> 8) & 0xFFu);
    frame[4] = (uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]);
    FakeUartPort_PushImuBytes(frame, sizeof(frame));
}

/** yaw 度数 → 器件 raw（int16，raw = deg/180*32768，截断到整数即可满足 near 断言）。 */
static int16_t yaw_raw(float deg)
{
    return (int16_t)(deg / 180.0f * 32768.0f);
}

/* 标准装配序：driver 先于 service（SysInit 同款）。 */
static void setup(void)
{
    s_fake_now_ms = 0u;
    FakeUartPort_ResetAll();
    Imu_Init();
    (void)vofa_init();
    ImuCheck_Start();
}

/* 抓取最近一帧并解出 count 个 LE float；返回帧长（0 = 无帧）。 */
static uint32_t copy_frame(float *out, uint32_t count)
{
    uint8_t raw[IMUC_FRAME_BYTES + 16u];
    uint32_t len = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    uint32_t i;

    if (len >= IMUC_FRAME_BYTES) {
        for (i = 0u; i < count; i++) {
            memcpy(&out[i], &raw[i * 4u], 4u);
        }
    }
    return len;
}

/* ---- 用例 ---------------------------------------------------------------- */

/* Start 后到期一帧 = 8 通道（36 字节）。 */
static int test_frame_has_eight_channels(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);                  /* 首拍只播种 */
    ImuCheck_Update(1010u);                  /* 到期 → 发帧 */
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    printf("PASS: test_frame_has_eight_channels\n");
    return 0;
}

/* 只读诊断：Start + 到期泵后，IMU 器件 TX 日志为空（不写器件）。 */
static int test_no_device_bytes_sent(void)
{
    uint8_t tx[8];

    setup();
    ImuCheck_Update(1000u);
    ImuCheck_Update(1010u);
    TEST_ASSERT_TRUE(FakeUartPort_CopyImuTxLog(tx, sizeof(tx)) == 0u);
    printf("PASS: test_no_device_bytes_sent\n");
    return 0;
}

/* 首拍只播种基准，不发帧。 */
static int test_first_tick_seeds_only(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == 0u);
    printf("PASS: test_first_tick_seeds_only\n");
    return 0;
}

/* 10ms 门控：不足周期不发帧，到期才发。 */
static int test_period_gating_10ms(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);                  /* 播种 */
    ImuCheck_Update(1005u);                  /* 5ms：未到期 */
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == 0u);
    ImuCheck_Update(1010u);                  /* 10ms：到期 */
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    printf("PASS: test_period_gating_10ms\n");
    return 0;
}

/* 忠实镜像：帧 ch0..ch7 恒等 Imu 快照/诊断（通道序 + 零第二处理）。 */
static int test_frame_mirrors_snapshot_and_diag(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    push_frame(RX_TYPE_YAW, 16384);          /* 90 度 */
    push_frame(RX_TYPE_GYRO, -16384);        /* 负角速度（量程由 driver 换算） */
    s_fake_now_ms = 1010u;                   /* 新鲜度时间戳与注入拍对齐 */
    ImuCheck_Update(1010u);
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);

    TEST_ASSERT_FLOAT_NEAR(ch[0], 90.0f, 0.01f);      /* yaw_deg */
    TEST_ASSERT_TRUE(ch[1] < 0.0f);                   /* yaw_rate 符号忠实（量程归 driver） */
    TEST_ASSERT_FLOAT_NEAR(ch[2], 0.0f, 0.5f);        /* age_ms：刚收到 */
    TEST_ASSERT_FLOAT_NEAR(ch[3], 1.0f, 0.001f);      /* valid */
    TEST_ASSERT_FLOAT_NEAR(ch[5], 2.0f, 0.001f);      /* frame_count = 2 */
    TEST_ASSERT_FLOAT_NEAR(ch[6], 0.0f, 0.001f);      /* checksum_errors */
    TEST_ASSERT_FLOAT_NEAR(ch[7], 0.0f, 0.001f);      /* rx_overflows */
    printf("PASS: test_frame_mirrors_snapshot_and_diag\n");
    return 0;
}

/* 校验和错帧计数镜像。 */
static int test_checksum_error_counted(void)
{
    float ch[IMUC_FRAME_CHANNELS];
    uint8_t bad[5] = { RX_HEAD, RX_TYPE_YAW, 0x00u, 0x40u, 0x00u }; /* 和校验错 */

    setup();
    ImuCheck_Update(1000u);
    FakeUartPort_PushImuBytes(bad, sizeof(bad));
    ImuCheck_Update(1010u);
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[6], 1.0f, 0.001f);
    printf("PASS: test_checksum_error_counted\n");
    return 0;
}

/* 无数据：valid=0、drift=0（快照 invalid 不参与漂移统计）。 */
static int test_invalid_snapshot_reports_zero(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    ImuCheck_Update(1010u);
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[3], 0.0f, 0.001f);      /* valid = 0 */
    TEST_ASSERT_FLOAT_NEAR(ch[4], 0.0f, 0.001f);      /* drift = 0 */
    printf("PASS: test_invalid_snapshot_reports_zero\n");
    return 0;
}

/* 短窗（<2s）drift 恒 0：即使 yaw 已变化。 */
static int test_drift_zero_before_min_window(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    push_frame(RX_TYPE_YAW, 0);              /* 基准 0 度（首个 valid 到期拍播种） */
    ImuCheck_Update(1010u);
    push_frame(RX_TYPE_YAW, yaw_raw(5.0f));  /* 1s 内漂 5 度 */
    FakeUartPort_CompleteVofaTx();
    ImuCheck_Update(2000u);                  /* 距基准 990ms < 2s */
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[4], 0.0f, 0.001f);
    printf("PASS: test_drift_zero_before_min_window\n");
    return 0;
}

/* 漂移数学：基准 0 度，3s 后 3 度 → drift ≈ 1.0 dps。 */
static int test_drift_computed_after_window(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    push_frame(RX_TYPE_YAW, 0);              /* t=1010 播种基准 0 度 */
    ImuCheck_Update(1010u);
    push_frame(RX_TYPE_YAW, yaw_raw(3.0f));
    FakeUartPort_CompleteVofaTx();
    ImuCheck_Update(4010u);                  /* 距基准 3000ms */
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[4], 1.0f, 0.05f);
    printf("PASS: test_drift_computed_after_window\n");
    return 0;
}

/* wrap 归一化：179° → −179° 的 +2° 跨界漂移不得被算成 −358°。 */
static int test_drift_wrap_normalized(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    push_frame(RX_TYPE_YAW, yaw_raw(179.0f));
    ImuCheck_Update(1010u);                  /* 基准 ≈ 179 度 */
    push_frame(RX_TYPE_YAW, yaw_raw(-179.0f));
    FakeUartPort_CompleteVofaTx();
    ImuCheck_Update(3010u);                  /* 2s 窗：期望 +2°/2s = +1 dps */
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[4], 1.0f, 0.05f);
    printf("PASS: test_drift_wrap_normalized\n");
    return 0;
}

/* GetTelemetry 与帧同源一致 + NULL 安全。 */
static int test_get_telemetry_matches_and_null_safe(void)
{
    float ch[IMUC_FRAME_CHANNELS];
    ImuCheck_Telemetry_T t;

    setup();
    ImuCheck_GetTelemetry(NULL);             /* 不崩即过 */
    ImuCheck_Update(1000u);
    push_frame(RX_TYPE_YAW, 16384);
    ImuCheck_Update(1010u);
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    ImuCheck_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.yaw_deg, ch[0], 1e-6f);
    TEST_ASSERT_TRUE(t.valid == true);
    TEST_ASSERT_TRUE(t.frame_count == 1u);
    printf("PASS: test_get_telemetry_matches_and_null_safe\n");
    return 0;
}

/* Stop 清表直接验证：Stop 后 vofa_run 不再发 8 通道帧（悬空指针帧防线）。 */
static int test_stop_clears_profile(void)
{
    uint8_t raw[IMUC_FRAME_BYTES + 16u];

    setup();
    ImuCheck_Update(1000u);
    ImuCheck_Update(1010u);                  /* 在飞一帧 8 通道 */
    ImuCheck_Stop();
    FakeUartPort_ResetAll();                 /* 清 TX 抓取，隔离 Stop 前的帧 */
    vofa_run();                              /* 空表运行：不得再发 8 通道数据帧 */
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) < IMUC_FRAME_BYTES);
    printf("PASS: test_stop_clears_profile\n");
    return 0;
}

/* Stop 清表后重 Start 可复用（重进条目场景）。 */
static int test_restart_after_stop(void)
{
    float ch[IMUC_FRAME_CHANNELS];

    setup();
    ImuCheck_Update(1000u);
    ImuCheck_Update(1010u);
    ImuCheck_Stop();
    FakeUartPort_ResetAll();

    ImuCheck_Start();
    ImuCheck_Update(2000u);                  /* 重进后首拍重新播种 */
    ImuCheck_Update(2010u);
    TEST_ASSERT_TRUE(copy_frame(ch, IMUC_FRAME_CHANNELS) == IMUC_FRAME_BYTES);
    printf("PASS: test_restart_after_stop\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_frame_has_eight_channels();
    failures += test_no_device_bytes_sent();
    failures += test_first_tick_seeds_only();
    failures += test_period_gating_10ms();
    failures += test_frame_mirrors_snapshot_and_diag();
    failures += test_checksum_error_counted();
    failures += test_invalid_snapshot_reports_zero();
    failures += test_drift_zero_before_min_window();
    failures += test_drift_computed_after_window();
    failures += test_drift_wrap_normalized();
    failures += test_get_telemetry_matches_and_null_safe();
    failures += test_stop_clears_profile();
    failures += test_restart_after_stop();

    if (failures != 0) {
        printf("%d imu_check test(s) failed.\n", failures);
        return 1;
    }

    printf("All imu_check service tests passed.\n");
    return 0;
}
