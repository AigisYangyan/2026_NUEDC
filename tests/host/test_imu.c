/*
 * 单轴 IMU Driver 主机测试。
 *
 * 覆盖：帧解析、量程换算、符号、校验和拒绝、失步重同步、跨调用分片、
 * 新鲜度、诊断计数、写指令字节序列。
 *
 * 不覆盖（无法在主机侧证明，须上板）：真实 UART3 收发、器件实际波特率、
 * 器件实际输出速率、航向角正方向。
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/imu/imu.h"

void FakeUartPort_ResetAll(void);
void FakeUartPort_PushImuBytes(const uint8_t *data, uint32_t length);
uint32_t FakeUartPort_CopyImuTxLog(uint8_t *out, uint32_t capacity);

/* ---- 主机侧时间源 ------------------------------------------------------- */
/* imu.c 通过 Clock_NowMs() 打新鲜度时间戳，通过 Mspm0Runtime_DelayMs() 满足
 * 器件要求的指令间延时。主机侧用可控假时钟替代，使 age_ms 可被确定性断言。 */

static uint32_t s_fake_now_ms = 0u;

uint32_t Clock_NowMs(void)
{
    return s_fake_now_ms;
}

void Mspm0Runtime_DelayMs(uint32_t delay_ms)
{
    s_fake_now_ms += delay_ms;
}

/* ---- 断言 --------------------------------------------------------------- */

static void expect_true(bool condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }

    printf("PASS: %s\n", name);
}

static bool near(float actual, float expected)
{
    float diff = actual - expected;

    if (diff < 0.0f) {
        diff = -diff;
    }

    return (diff < 0.01f);
}

/* ---- 夹具 --------------------------------------------------------------- */

#define RX_HEAD      0x5Au
#define RX_TYPE_GYRO 0xAAu
#define RX_TYPE_YAW  0xBBu

static void reset_all(void)
{
    s_fake_now_ms = 0u;
    FakeUartPort_ResetAll();
    Imu_Init();
}

/** 组一个校验和正确的读帧并推入端口 FIFO。 */
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

static Imu_Snapshot_t snapshot_after_update(void)
{
    Imu_Snapshot_t snap;

    memset(&snap, 0, sizeof(snap));
    Imu_Update();
    Imu_GetSnapshot(&snap);

    return snap;
}

/* ---- 用例 --------------------------------------------------------------- */

static void test_init_snapshot_is_invalid(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    memset(&snap, 0xFF, sizeof(snap));
    Imu_GetSnapshot(&snap);

    expect_true((snap.valid == false) && (snap.age_ms == 0u),
                "imu init snapshot is invalid with zero age");
}

static void test_yaw_frame_decodes_full_scale(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    /* 16384 = 32768/2 -> 180 * 0.5 = 90 度。 */
    push_frame(RX_TYPE_YAW, 16384);
    snap = snapshot_after_update();

    expect_true((snap.valid != false) && near(snap.yaw_deg, 90.0f),
                "imu yaw frame decodes half full-scale to 90 deg");
}

static void test_gyro_frame_decodes_full_scale(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    /* 16384 = 32768/2 -> 2000 * 0.5 = 1000 度/秒。 */
    push_frame(RX_TYPE_GYRO, 16384);
    snap = snapshot_after_update();

    expect_true((snap.valid != false) && near(snap.yaw_rate_dps, 1000.0f),
                "imu gyro frame decodes half full-scale to 1000 dps");
}

static void test_yaw_frame_decodes_negative(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    push_frame(RX_TYPE_YAW, -16384);
    snap = snapshot_after_update();

    expect_true(near(snap.yaw_deg, -90.0f),
                "imu yaw frame decodes negative raw as negative angle");
}

static void test_gyro_frame_decodes_negative(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    push_frame(RX_TYPE_GYRO, -16384);
    snap = snapshot_after_update();

    expect_true(near(snap.yaw_rate_dps, -1000.0f),
                "imu gyro frame decodes negative raw as negative rate");
}

static void test_both_types_update_independently(void)
{
    Imu_Snapshot_t snap;
    Imu_Diag_t diag;

    reset_all();
    push_frame(RX_TYPE_YAW, 16384);
    push_frame(RX_TYPE_GYRO, -8192);
    snap = snapshot_after_update();
    Imu_GetDiag(&diag);

    expect_true(near(snap.yaw_deg, 90.0f) && near(snap.yaw_rate_dps, -500.0f) &&
                    (diag.frame_count == 2u),
                "imu yaw and gyro frames update independent fields");
}

static void test_bad_checksum_rejected(void)
{
    Imu_Snapshot_t snap;
    Imu_Diag_t diag;
    /* 正确校验和为 0x55；这里故意写 0x56。 */
    const uint8_t frame[] = {0x5Au, 0xBBu, 0x00u, 0x40u, 0x56u};

    reset_all();
    FakeUartPort_PushImuBytes(frame, sizeof(frame));
    snap = snapshot_after_update();
    Imu_GetDiag(&diag);

    expect_true((snap.valid == false) && (diag.frame_count == 0u) &&
                    (diag.checksum_error_count == 1u),
                "imu bad checksum is rejected and counted");
}

static void test_unknown_type_ignored_without_checksum_error(void)
{
    Imu_Diag_t diag;
    /* datasheet 的 BIAS_CAL 状态回包 5A CC 00 00 96 —— 其校验和按手册自身
     * 规则应为 0x26，手册自相矛盾。本 Driver 不解析该类型，且不得把它
     * 计成校验和错误，否则诊断计数会被无关帧污染。 */
    const uint8_t frame[] = {0x5Au, 0xCCu, 0x00u, 0x00u, 0x96u};

    reset_all();
    FakeUartPort_PushImuBytes(frame, sizeof(frame));
    Imu_Update();
    Imu_GetDiag(&diag);

    expect_true((diag.frame_count == 0u) && (diag.checksum_error_count == 0u),
                "imu unknown frame type is ignored without checksum error");
}

static void test_resync_recovers_frame_after_stray_head(void)
{
    Imu_Snapshot_t snap;
    /* 前缀一个游离的 0x5A。若解析器在失败时整帧丢弃 5 字节，真正的帧起点
     * 会被一起吞掉；滑动窗口必须只丢最早一个字节。 */
    const uint8_t stream[] = {0x5Au, 0x5Au, 0xBBu, 0x00u, 0x40u, 0x55u};

    reset_all();
    FakeUartPort_PushImuBytes(stream, sizeof(stream));
    snap = snapshot_after_update();

    expect_true((snap.valid != false) && near(snap.yaw_deg, 90.0f),
                "imu resyncs and recovers frame after stray head byte");
}

static void test_garbage_prefix_then_valid_frame(void)
{
    Imu_Snapshot_t snap;
    const uint8_t garbage[] = {0x11u, 0x22u, 0x33u};

    reset_all();
    FakeUartPort_PushImuBytes(garbage, sizeof(garbage));
    push_frame(RX_TYPE_YAW, -16384);
    snap = snapshot_after_update();

    expect_true((snap.valid != false) && near(snap.yaw_deg, -90.0f),
                "imu parses valid frame following garbage prefix");
}

static void test_frame_split_across_updates(void)
{
    Imu_Snapshot_t snap;
    const uint8_t first[] = {0x5Au, 0xBBu, 0x00u};
    const uint8_t second[] = {0x40u, 0x55u};

    reset_all();
    FakeUartPort_PushImuBytes(first, sizeof(first));
    snap = snapshot_after_update();
    expect_true(snap.valid == false,
                "imu partial frame does not produce a snapshot");

    FakeUartPort_PushImuBytes(second, sizeof(second));
    snap = snapshot_after_update();
    expect_true((snap.valid != false) && near(snap.yaw_deg, 90.0f),
                "imu frame split across two updates parses once complete");
}

static void test_age_ms_tracks_clock(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    s_fake_now_ms = 1000u;
    push_frame(RX_TYPE_YAW, 0);
    (void)snapshot_after_update();

    s_fake_now_ms = 1250u;
    Imu_GetSnapshot(&snap);

    expect_true((snap.valid != false) && (snap.age_ms == 250u),
                "imu age_ms grows with clock since last valid frame");
}

static void test_age_ms_resets_on_new_frame(void)
{
    Imu_Snapshot_t snap;

    reset_all();
    s_fake_now_ms = 1000u;
    push_frame(RX_TYPE_YAW, 0);
    (void)snapshot_after_update();

    s_fake_now_ms = 5000u;
    push_frame(RX_TYPE_GYRO, 0);
    snap = snapshot_after_update();

    expect_true(snap.age_ms == 0u,
                "imu age_ms resets to zero on a fresh frame");
}

static void test_zero_yaw_sends_unlock_zero_save(void)
{
    uint8_t log[32];
    uint32_t len = 0u;
    const uint8_t expected[] = {
        0x55u, 0xAAu, 0x13u, 0x8Eu, 0x5Fu, /* 解锁 */
        0x55u, 0xAAu, 0x15u, 0x00u, 0x00u, /* Z 轴归零 */
        0x55u, 0xAAu, 0x00u, 0x00u, 0x00u, /* 保存 */
    };

    reset_all();
    expect_true(Imu_ZeroYaw() != false, "imu zero yaw reports success");

    len = FakeUartPort_CopyImuTxLog(log, sizeof(log));
    expect_true((len == sizeof(expected)) &&
                    (memcmp(log, expected, sizeof(expected)) == 0),
                "imu zero yaw sends unlock, yaw-zero, save in order");
}

static void test_set_output_rate_sends_expected_code(void)
{
    uint8_t log[32];
    uint32_t len = 0u;
    /* 200 Hz 的 RRATE 编码为 0x0B，寄存器地址 0x02。 */
    const uint8_t expected[] = {
        0x55u, 0xAAu, 0x13u, 0x8Eu, 0x5Fu,
        0x55u, 0xAAu, 0x02u, 0x0Bu, 0x00u,
        0x55u, 0xAAu, 0x00u, 0x00u, 0x00u,
    };

    reset_all();
    expect_true(Imu_SetOutputRate(IMU_OUTPUT_RATE_200_HZ) != false,
                "imu set output rate reports success");

    len = FakeUartPort_CopyImuTxLog(log, sizeof(log));
    expect_true((len == sizeof(expected)) &&
                    (memcmp(log, expected, sizeof(expected)) == 0),
                "imu set output rate 200Hz sends register code 0x0B");
}

static void test_set_output_rate_rejects_out_of_range(void)
{
    uint8_t log[32];

    reset_all();
    expect_true(Imu_SetOutputRate((Imu_OutputRate_t)99) == false,
                "imu set output rate rejects out-of-range enum");
    expect_true(FakeUartPort_CopyImuTxLog(log, sizeof(log)) == 0u,
                "imu rejected output rate sends no bytes");
}

static void test_rx_overflow_surfaced_in_diag(void)
{
    Imu_Diag_t diag;
    uint8_t flood[512];
    uint32_t index = 0u;

    reset_all();
    for (index = 0u; index < sizeof(flood); index++) {
        flood[index] = 0x00u;
    }
    FakeUartPort_PushImuBytes(flood, sizeof(flood));

    Imu_Update();
    Imu_GetDiag(&diag);

    expect_true(diag.rx_overflow_count > 0u,
                "imu port fifo overflow is surfaced through diag");
}

static void test_null_out_is_safe(void)
{
    reset_all();
    push_frame(RX_TYPE_YAW, 16384);
    Imu_Update();

    Imu_GetSnapshot(NULL);
    Imu_GetDiag(NULL);

    expect_true(true, "imu null snapshot and diag pointers are safe");
}

int main(void)
{
    test_init_snapshot_is_invalid();
    test_yaw_frame_decodes_full_scale();
    test_gyro_frame_decodes_full_scale();
    test_yaw_frame_decodes_negative();
    test_gyro_frame_decodes_negative();
    test_both_types_update_independently();
    test_bad_checksum_rejected();
    test_unknown_type_ignored_without_checksum_error();
    test_resync_recovers_frame_after_stray_head();
    test_garbage_prefix_then_valid_frame();
    test_frame_split_across_updates();
    test_age_ms_tracks_clock();
    test_age_ms_resets_on_new_frame();
    test_zero_yaw_sends_unlock_zero_save();
    test_set_output_rate_sends_expected_code();
    test_set_output_rate_rejects_out_of_range();
    test_rx_overflow_surfaced_in_diag();
    test_null_out_is_safe();

    printf("all imu tests passed\n");
    return 0;
}
