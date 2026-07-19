/**
 * @file    test_encoder_test.c
 * @brief   编码器脉冲遥测诊断服务主机测试（W3-T2，契约 §23.2）。
 *
 * 链接组成：真实 encoder_test.c + encoder.c + uart_vofa.c + board_uart×4
 *           + fake_board_gpio.c（编码器原始注入）+ fake_uart_port.c（VOFA TX 抓取）。
 * 不链 fake_clock：encoder_test 取 now_ms 参数注入，不直读 Clock。
 *
 * 断言策略：帧内容 = FakeUartPort_CopyVofaTx 解 4 个 LE float（累计脉冲 int→float 精确）；
 * 方向绝对正负是硬件问题（诊断在板上答），故断言「忠实镜像快照 + 累计一致 + 反向翻号」——
 * 不依赖 encoder.c 默认 s_direction_sign 的具体取值。
 */
#include "app/service/encoder_test/encoder_test.h"

#include "driver/encoder/encoder.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* fake 注入/抓取接口 */
extern void FakeBoardGpio_SetRaw(int32_t left, int32_t right);
extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length);
extern void FakeUartPort_CompleteVofaTx(void);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

#define ENC_FRAME_CHANNELS 4u
#define ENC_FRAME_BYTES (ENC_FRAME_CHANNELS * 4u + 4u) /* 4 通道 + JustFloat 4 字节帧尾 = 20 */

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

/* 标准装配序：与 SysInit 一致（Encoder/vofa 先于 Service）。 */
static void setup(void)
{
    FakeBoardGpio_SetRaw(0, 0);
    Encoder_Init();
    (void)vofa_init();          /* 内部 VofaUart_Init + 清 profile */
    FakeUartPort_ResetAll();
    EncoderTest_Start();
}

/* 抓取最近一帧并解出 count 个 LE float；返回帧长（0 = 无帧）。 */
static uint32_t copy_frame(float *out, uint32_t count)
{
    uint8_t raw[ENC_FRAME_BYTES + 16u];
    uint32_t len = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    uint32_t i;

    if (len >= ENC_FRAME_BYTES) {
        for (i = 0u; i < count; i++) {
            memcpy(&out[i], &raw[i * 4u], 4u);
        }
    }
    return len;
}

/* 播种基准 + 采一帧：seed(t0) → SetRaw → pump(t0+10) 到期发帧。 */
static uint32_t sample_frame(float *out, uint32_t t0,
                             int32_t raw_left, int32_t raw_right)
{
    EncoderTest_Update(t0);                 /* 首拍只播种基准 */
    FakeBoardGpio_SetRaw(raw_left, raw_right);
    EncoderTest_Update(t0 + 10u);           /* elapsed 10 → 采样 + 发帧 */
    return copy_frame(out, ENC_FRAME_CHANNELS);
}

/* tx×4：Start 后到期一帧 = 4 通道（20 字节），无 cmd 绑定路径（只读诊断）。 */
static int test_frame_has_four_channels(void)
{
    float ch[ENC_FRAME_CHANNELS];

    setup();
    TEST_ASSERT_TRUE(sample_frame(ch, 1000u, 100, 0) == ENC_FRAME_BYTES);
    printf("PASS: test_frame_has_four_channels\n");
    return 0;
}

/* 忠实镜像：帧 ch0..3 恒等 Encoder_GetSnapshot（通道序 + 零第二处理）。 */
static int test_frame_mirrors_snapshot(void)
{
    float ch[ENC_FRAME_CHANNELS];
    Encoder_Snapshot snap;

    setup();
    TEST_ASSERT_TRUE(sample_frame(ch, 1000u, 320, -150) == ENC_FRAME_BYTES);
    Encoder_GetSnapshot(&snap);

    TEST_ASSERT_FLOAT_NEAR(ch[0], (float)snap.total_pulses[ENCODER_LEFT], 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(ch[1], (float)snap.total_pulses[ENCODER_RIGHT], 1e-3f);
    TEST_ASSERT_FLOAT_NEAR(ch[2], snap.speed_mps[ENCODER_LEFT], 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ch[3], snap.speed_mps[ENCODER_RIGHT], 1e-6f);
    printf("PASS: test_frame_mirrors_snapshot\n");
    return 0;
}

/* 累计一致：连续同向 raw 步进 → enc_L 单调同号、幅值累加（正转脉冲正增语义）。 */
static int test_forward_accumulates_consistent_sign(void)
{
    float ch[ENC_FRAME_CHANNELS];
    float v1;
    float v2;

    setup();
    EncoderTest_Update(1000u);                  /* 播种 */

    FakeBoardGpio_SetRaw(100, 0);
    EncoderTest_Update(1010u);
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(copy_frame(ch, ENC_FRAME_CHANNELS) == ENC_FRAME_BYTES);
    v1 = ch[0];

    FakeBoardGpio_SetRaw(200, 0);               /* 再同向 +100 */
    EncoderTest_Update(1020u);
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(copy_frame(ch, ENC_FRAME_CHANNELS) == ENC_FRAME_BYTES);
    v2 = ch[0];

    TEST_ASSERT_TRUE(fabsf(v1) > 0.5f);         /* 首步非零 */
    TEST_ASSERT_TRUE(v1 * v2 > 0.0f);           /* 两步同号（方向一致） */
    TEST_ASSERT_TRUE(fabsf(v2) > fabsf(v1));    /* 幅值累加 */
    printf("PASS: test_forward_accumulates_consistent_sign\n");
    return 0;
}

/* 反向翻号：raw 反向步进 → enc_L 增量符号相对正向翻转（接线反可检出）。 */
static int test_reverse_flips_sign(void)
{
    float ch[ENC_FRAME_CHANNELS];
    float fwd_delta;
    float rev_delta;
    float prev;

    setup();
    EncoderTest_Update(1000u);                  /* 播种 */

    FakeBoardGpio_SetRaw(100, 0);               /* 正向 +100 */
    EncoderTest_Update(1010u);
    FakeUartPort_CompleteVofaTx();
    (void)copy_frame(ch, ENC_FRAME_CHANNELS);
    fwd_delta = ch[0];                          /* 从 0 起 = 正向增量 */
    prev = ch[0];

    FakeBoardGpio_SetRaw(50, 0);                /* 反向 -50 */
    EncoderTest_Update(1020u);
    FakeUartPort_CompleteVofaTx();
    (void)copy_frame(ch, ENC_FRAME_CHANNELS);
    rev_delta = ch[0] - prev;

    TEST_ASSERT_TRUE(fabsf(fwd_delta) > 0.5f);
    TEST_ASSERT_TRUE(fwd_delta * rev_delta < 0.0f); /* 反向增量与正向反号 */
    printf("PASS: test_reverse_flips_sign\n");
    return 0;
}

/* 10ms 门控：未到期不发帧，到期发一帧。 */
static int test_ten_ms_gating(void)
{
    uint8_t raw[ENC_FRAME_BYTES + 16u];

    setup();
    EncoderTest_Update(1000u);                  /* 播种 */
    FakeBoardGpio_SetRaw(100, 0);

    EncoderTest_Update(1005u);                  /* elapsed 5 < 10 → 不发 */
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u);

    EncoderTest_Update(1010u);                  /* elapsed 10 → 发 */
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == ENC_FRAME_BYTES);
    printf("PASS: test_ten_ms_gating\n");
    return 0;
}

/* Stop 清 profile：此后 Update 不再发帧（g_data_count=0 → send 早返回）。 */
static int test_stop_clears_profile(void)
{
    uint8_t raw[ENC_FRAME_BYTES + 16u];

    setup();
    (void)sample_frame((float[ENC_FRAME_CHANNELS]){0}, 1000u, 100, 0); /* 先证有帧 */
    FakeUartPort_CompleteVofaTx();

    EncoderTest_Stop();
    FakeUartPort_ResetAll();
    FakeBoardGpio_SetRaw(500, 500);
    EncoderTest_Update(2000u);
    EncoderTest_Update(2010u);
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u); /* 无帧 */
    printf("PASS: test_stop_clears_profile\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_frame_has_four_channels();
    failures += test_frame_mirrors_snapshot();
    failures += test_forward_accumulates_consistent_sign();
    failures += test_reverse_flips_sign();
    failures += test_ten_ms_gating();
    failures += test_stop_clears_profile();

    if (failures != 0) {
        printf("encoder_test service tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll encoder_test service tests passed.\n");
    return 0;
}
