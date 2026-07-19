/**
 * @file    test_gray_check.c
 * @brief   12 路灰度数字量遥测诊断服务主机测试（W4，契约 §24）。
 *
 * 链接组成：真实 gray_check.c + gray.c + uart_vofa.c + board_uart×4
 *           + fake_gray_port.c（灰度位图注入）+ fake_uart_port.c（VOFA TX 抓取）。
 * 不链 fake_clock：gray_check 取 now_ms 参数注入，不直读 Clock。
 *
 * 断言策略：帧内容 = FakeUartPort_CopyVofaTx 解 12 个 LE float（int 0/1→float 精确 0.0/1.0）；
 * 验「忠实镜像注入位图 + 通道序恒等（bit i→ch i，无左右重排/反相）+ 10ms 门控 + Stop 清组」。
 * 不验车上左右——那是硬件接线，现场肉眼答（gray.h 位序警告）。
 */
#include "app/service/gray_check/gray_check.h"

#include "driver/gray/gray.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* fake 注入/抓取接口 */
extern void FakeGrayPort_Reset(void);
extern void FakeGrayPort_SetDarkChannels(uint16_t channel_bitmap);
extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_CompleteVofaTx(void);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

#define GRAY_FRAME_CHANNELS 12u
#define GRAY_FRAME_BYTES (GRAY_FRAME_CHANNELS * 4u + 4u) /* 12 通道 + JustFloat 4 字节帧尾 = 52 */

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 标准装配序：与 SysInit 一致（vofa 先于 Service）；灰度无 Init。 */
static void setup(void)
{
    FakeGrayPort_Reset();
    (void)vofa_init();          /* 内部 VofaUart_Init + 清 profile */
    FakeUartPort_ResetAll();
    GrayCheck_Start();
}

/* 抓取最近一帧并解出 count 个 LE float；返回帧长（0 = 无帧）。 */
static uint32_t copy_frame(float *out, uint32_t count)
{
    uint8_t raw[GRAY_FRAME_BYTES + 16u];
    uint32_t len = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    uint32_t i;

    if (len >= GRAY_FRAME_BYTES) {
        for (i = 0u; i < count; i++) {
            memcpy(&out[i], &raw[i * 4u], 4u);
        }
    }
    return len;
}

/* tx×12：Start 后（base=0）首拍即到期一帧 = 12 通道（52 字节），无 cmd 绑定路径（只读诊断）。 */
static int test_frame_has_twelve_channels(void)
{
    float ch[GRAY_FRAME_CHANNELS];

    setup();
    FakeGrayPort_SetDarkChannels(0x000u);
    GrayCheck_Update(1000u);                    /* base=0 → 首拍即发 */
    TEST_ASSERT_TRUE(copy_frame(ch, GRAY_FRAME_CHANNELS) == GRAY_FRAME_BYTES);
    printf("PASS: test_frame_has_twelve_channels\n");
    return 0;
}

/* 忠实镜像：注入任意位图 → 帧 ch_i 恒等 (bitmap>>i)&1（深色=1、浅色=0，零第二处理）。 */
static int test_frame_mirrors_bitmap(void)
{
    const uint16_t pattern = 0x825u;            /* bit 0,2,5,11 深色 = 0b1000_0010_0101 */
    float ch[GRAY_FRAME_CHANNELS];
    uint32_t i;

    setup();
    FakeGrayPort_SetDarkChannels(pattern);
    GrayCheck_Update(1000u);
    TEST_ASSERT_TRUE(copy_frame(ch, GRAY_FRAME_CHANNELS) == GRAY_FRAME_BYTES);

    for (i = 0u; i < GRAY_FRAME_CHANNELS; i++) {
        float expected = (((pattern >> i) & 1u) != 0u) ? 1.0f : 0.0f;
        TEST_ASSERT_TRUE(fabsf(ch[i] - expected) < 0.5f); /* 0/1 well-separated */
    }
    printf("PASS: test_frame_mirrors_bitmap\n");
    return 0;
}

/* 通道序恒等：逐路只置一路深色 → 只有对应 ch 亮、其余全灭。catch 任何左右重排/反相/错位。 */
static int test_channel_order_no_remap(void)
{
    float ch[GRAY_FRAME_CHANNELS];
    uint32_t k;
    uint32_t i;

    setup();
    for (k = 0u; k < GRAY_FRAME_CHANNELS; k++) {
        FakeUartPort_ResetAll();                /* 清 TX 抓取；profile 注册不受影响 */
        FakeGrayPort_SetDarkChannels((uint16_t)(1u << k));
        GrayCheck_Update(1000u + (k + 1u) * 10u); /* 每次都到期（≥10ms 门控） */
        TEST_ASSERT_TRUE(copy_frame(ch, GRAY_FRAME_CHANNELS) == GRAY_FRAME_BYTES);

        for (i = 0u; i < GRAY_FRAME_CHANNELS; i++) {
            float expected = (i == k) ? 1.0f : 0.0f;
            TEST_ASSERT_TRUE(fabsf(ch[i] - expected) < 0.5f);
        }
        FakeUartPort_CompleteVofaTx();
    }
    printf("PASS: test_channel_order_no_remap\n");
    return 0;
}

/* 10ms 门控：首拍发帧后，未到期不发、到期发一帧。 */
static int test_ten_ms_gating(void)
{
    uint8_t raw[GRAY_FRAME_BYTES + 16u];

    setup();
    FakeGrayPort_SetDarkChannels(0xFFFu);
    GrayCheck_Update(1000u);                    /* base=0 → 首拍发帧，base←1000 */
    FakeUartPort_CompleteVofaTx();
    FakeUartPort_ResetAll();                     /* 清 TX 抓取 */

    GrayCheck_Update(1005u);                     /* elapsed 5 < 10 → 不发 */
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u);

    GrayCheck_Update(1010u);                     /* elapsed 10 → 发 */
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == GRAY_FRAME_BYTES);
    printf("PASS: test_ten_ms_gating\n");
    return 0;
}

/* Stop 清 profile：此后 Update 不再发帧（g_data_count=0 → send 早返回）。 */
static int test_stop_clears_profile(void)
{
    uint8_t raw[GRAY_FRAME_BYTES + 16u];

    setup();
    FakeGrayPort_SetDarkChannels(0x0FFu);
    GrayCheck_Update(1000u);                     /* 先证有帧 */
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == GRAY_FRAME_BYTES);
    FakeUartPort_CompleteVofaTx();

    GrayCheck_Stop();
    FakeUartPort_ResetAll();
    FakeGrayPort_SetDarkChannels(0xFFFu);
    GrayCheck_Update(2000u);
    GrayCheck_Update(2010u);
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u); /* 无帧 */
    printf("PASS: test_stop_clears_profile\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_frame_has_twelve_channels();
    failures += test_frame_mirrors_bitmap();
    failures += test_channel_order_no_remap();
    failures += test_ten_ms_gating();
    failures += test_stop_clears_profile();

    if (failures != 0) {
        printf("gray_check service tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll gray_check service tests passed.\n");
    return 0;
}
