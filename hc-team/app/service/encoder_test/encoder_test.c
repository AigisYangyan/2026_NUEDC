/**
 * @file    encoder_test.c
 * @brief   编码器脉冲遥测诊断服务实现：VOFA tx×4 只读遥测 + 10ms 自门控采样/发帧。
 *
 * 数据链（§8.2）：
 *   GPIO/QEI → BoardGpio 原始快照 → Encoder_Update(elapsed) [encoder.c：模差 + s_direction_sign
 *   方向修正 + m/s 换算] → Encoder_GetSnapshot [已修正的累计脉冲/线速度]
 *   → 本服务 tx 镜像（单向复制，零第二处理）→ vofa_run [uart_vofa：JustFloat 组帧发送]。
 *
 * 所有权：方向/单位换算唯一在 encoder.c；本服务只复制快照，不反向、不缩放、不滤波。
 */
#include "app/service/encoder_test/encoder_test.h"

#include "driver/encoder/encoder.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <stdbool.h>

#define ENCODER_TEST_STREAM_PERIOD_MS 10u

/* tx 遥测镜像（注册序 = 上位机通道序）：
 * ch0=enc_L 累计脉冲(int)、ch1=enc_R、ch2=spd_L(m/s,float)、ch3=spd_R。
 * 累计脉冲经 register_int→JustFloat 转 float 发送（|脉冲|<2^24 精确，100 米约 1e6 脉冲无损）。 */
static int   s_tx_enc_left;
static int   s_tx_enc_right;
static float s_tx_spd_left;
static float s_tx_spd_right;

static bool     s_seeded;
static uint32_t s_period_base_ms;

static void encoder_test_refresh_tx(void)
{
    Encoder_Snapshot snap;

    Encoder_GetSnapshot(&snap);
    /* 单向复制已方向修正 + 单位换算的快照，零第二处理（§8.2 单一所有者）。 */
    s_tx_enc_left  = (int)snap.total_pulses[ENCODER_LEFT];
    s_tx_enc_right = (int)snap.total_pulses[ENCODER_RIGHT];
    s_tx_spd_left  = snap.speed_mps[ENCODER_LEFT];
    s_tx_spd_right = snap.speed_mps[ENCODER_RIGHT];
}

void EncoderTest_Start(void)
{
    vofa_clear_profile();

    s_tx_enc_left  = 0;
    s_tx_enc_right = 0;
    s_tx_spd_left  = 0.0f;
    s_tx_spd_right = 0.0f;

    /* 注册顺序 = 通道序 ch0..ch3；零 bind_cmd（只读诊断，不接收上位机命令）。 */
    (void)vofa_register_int(&s_tx_enc_left);
    (void)vofa_register_int(&s_tx_enc_right);
    (void)vofa_register_float(&s_tx_spd_left);
    (void)vofa_register_float(&s_tx_spd_right);

    s_seeded = false;
    s_period_base_ms = 0u;
}

void EncoderTest_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if (!s_seeded) {
        /* 首拍无历史时刻可算 elapsed：只播种采样基准。 */
        s_seeded = true;
        s_period_base_ms = now_ms;
        return;
    }

    elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */
    if (elapsed_ms < ENCODER_TEST_STREAM_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    /* 第二 Encoder_Update 采样点（V21 扩条，单活动条目互斥）；elapsed>0 满足 encoder 前置。 */
    (void)Encoder_Update(elapsed_ms);
    encoder_test_refresh_tx();
    vofa_run(); /* RX 排空（零绑定 = 无副作用）+ 发本拍刚刷新的遥测帧（刷新在发送前，无一帧延迟） */
}

void EncoderTest_Stop(void)
{
    vofa_clear_profile();
}
