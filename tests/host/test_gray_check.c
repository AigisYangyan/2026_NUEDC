/**
 * @file    test_gray_check.c
 * @brief   12 路灰度数字量遥测诊断服务主机测试（W4，契约 §24）。
 *
 * 链接组成：真实 gray_check.c + gray.c + uart_vofa.c + board_uart×4
 *           + fake_gray_port.c（灰度位图注入）+ fake_uart_port.c（VOFA TX 抓取）
 *           + fake_hmi.c（OLED 面板行文本/绘制计数捕获，W7）。
 * 不链 fake_clock：gray_check 取 now_ms 参数注入，不直读 Clock。
 *
 * 断言策略：帧内容 = FakeUartPort_CopyVofaTx 解 12 个 LE float（int 0/1→float 精确 0.0/1.0）；
 * 验「忠实镜像注入位图 + 通道序恒等（bit i→ch i，无左右重排/反相）+ 10ms 门控 + Stop 清组」。
 * W7 标定助手面板：FakeHmi_GetRow 精确行文本断言（L/S/T/X 四行）+ 逐行绘制计数验
 * 「100ms 面板门控 + 行差分不重绘 + 重进条目清零全重绘」。
 * 不验车上左右——那是硬件接线，现场肉眼答（gray.h 位序警告）。
 */
#include "app/service/gray_check/gray_check.h"

#include "driver/gray/gray.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* fake 注入/抓取接口 */
extern void FakeGrayPort_Reset(void);
extern void FakeGrayPort_SetDarkChannels(uint16_t channel_bitmap);
extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_CompleteVofaTx(void);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);
extern void FakeHmi_Reset(void);
extern void FakeHmi_SetReady(bool ready);
extern const char *FakeHmi_GetRow(uint8_t row);
extern uint32_t FakeHmi_GetRowPrintCount(uint8_t row);

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
    FakeHmi_Reset();
    (void)vofa_init();          /* 内部 VofaUart_Init + 清 profile */
    FakeUartPort_ResetAll();
    GrayCheck_Start();
}

#define TEST_ASSERT_ROW(row, expected) do { \
    if (strcmp(FakeHmi_GetRow(row), (expected)) != 0) { \
        printf("FAIL: row%u \"%s\" != \"%s\" at %s:%d\n", (unsigned)(row), \
               FakeHmi_GetRow(row), (expected), __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

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

/* ---- W7 标定助手 OLED 面板用例（契约 §29.4 E04）---------------------------- */

/* 首绘：进条目首拍面板即绘全 4 行（缓存空→全变化），L/S 镜像位图、T 全零、X 行十六进制+路数。 */
static int test_panel_first_paint_rows(void)
{
    setup();
    FakeGrayPort_SetDarkChannels(0x825u);       /* bit 0,2,5,11 深色 */
    GrayCheck_Update(1000u);                    /* base=0 → 首拍采样+面板首绘 */

    TEST_ASSERT_ROW(0u, "L:#.#..#.....#");
    TEST_ASSERT_ROW(1u, "S:#.#..#.....#");
    TEST_ASSERT_ROW(2u, "T:............");      /* 首拍只播种 prev，不计跳变 */
    TEST_ASSERT_ROW(3u, "X:825 N:04");
    printf("PASS: test_panel_first_paint_rows\n");
    return 0;
}

/* 粘滞累积：深色消失后 L 行回浅色，S 行保持曾深色的通道（进条目以来 OR）。 */
static int test_panel_sticky_accumulates(void)
{
    setup();
    FakeGrayPort_SetDarkChannels(0x00Fu);
    GrayCheck_Update(1000u);
    FakeUartPort_CompleteVofaTx();
    FakeGrayPort_SetDarkChannels(0x000u);
    GrayCheck_Update(1100u);                    /* 采样+面板均到期 */

    TEST_ASSERT_ROW(0u, "L:............");
    TEST_ASSERT_ROW(1u, "S:####........");
    TEST_ASSERT_ROW(2u, "T:1111........");      /* 四路各翻转一次 */
    TEST_ASSERT_ROW(3u, "X:000 N:00");
    printf("PASS: test_panel_sticky_accumulates\n");
    return 0;
}

/* 跳变计数：单路反复翻转，1..9 显示数字、≥10 显示 '*'（饱和标记）。 */
static int test_panel_toggle_count_saturates(void)
{
    uint32_t t;
    uint32_t k;

    setup();
    FakeGrayPort_SetDarkChannels(0x001u);
    GrayCheck_Update(1000u);                    /* 播种 prev=0x001 */
    FakeUartPort_CompleteVofaTx();

    t = 1000u;
    for (k = 0u; k < 10u; k++) {                /* 每 10ms 翻转一次 ch0，共 10 次跳变 */
        t += 10u;
        FakeGrayPort_SetDarkChannels((k % 2u == 0u) ? 0x000u : 0x001u);
        GrayCheck_Update(t);
        FakeUartPort_CompleteVofaTx();
    }
    /* t=1100 恰逢面板 100ms 到期：ch0 跳变数=10 → '*'；末拍位图 0x001。 */
    TEST_ASSERT_ROW(2u, "T:*...........");
    TEST_ASSERT_ROW(0u, "L:#...........");
    printf("PASS: test_panel_toggle_count_saturates\n");
    return 0;
}

/* 行差分：内容不变的行不重绘（同位图第二个面板周期零 PrintLine）。 */
static int test_panel_row_diff_no_redraw(void)
{
    uint32_t row;

    setup();
    FakeGrayPort_SetDarkChannels(0x0F0u);
    GrayCheck_Update(1000u);                    /* 首绘 4 行各一次 */
    FakeUartPort_CompleteVofaTx();
    for (row = 0u; row < 4u; row++) {
        TEST_ASSERT_TRUE(FakeHmi_GetRowPrintCount((uint8_t)row) == 1u);
    }

    GrayCheck_Update(1100u);                    /* 同位图、无跳变 → 4 行内容全同 → 零重绘 */
    for (row = 0u; row < 4u; row++) {
        TEST_ASSERT_TRUE(FakeHmi_GetRowPrintCount((uint8_t)row) == 1u);
    }
    printf("PASS: test_panel_row_diff_no_redraw\n");
    return 0;
}

/* 面板 100ms 门控：位图已变但面板未到期不重绘；到期后按新内容重绘。 */
static int test_panel_hundred_ms_gating(void)
{
    setup();
    FakeGrayPort_SetDarkChannels(0x001u);
    GrayCheck_Update(1000u);                    /* 首绘：L:#... */
    FakeUartPort_CompleteVofaTx();

    FakeGrayPort_SetDarkChannels(0x002u);
    GrayCheck_Update(1010u);                    /* 采样到期、面板未到期（10<100）→ 不重绘 */
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_ROW(0u, "L:#...........");
    TEST_ASSERT_TRUE(FakeHmi_GetRowPrintCount(0u) == 1u);

    GrayCheck_Update(1100u);                    /* 面板到期 → 按当前位图重绘 */
    TEST_ASSERT_ROW(0u, "L:.#..........");
    TEST_ASSERT_TRUE(FakeHmi_GetRowPrintCount(0u) == 2u);
    printf("PASS: test_panel_hundred_ms_gating\n");
    return 0;
}

/* PrintLine 失败（显示未就绪/总线错）→ 行缓存不更新 → 下个面板周期重试整行成功。 */
static int test_panel_retry_after_not_ready(void)
{
    setup();
    FakeGrayPort_SetDarkChannels(0x001u);
    FakeHmi_SetReady(false);
    GrayCheck_Update(1000u);                    /* 面板到期但 PrintLine 全拒 → 零绘制、缓存不更新 */
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(FakeHmi_GetRowPrintCount(0u) == 0u);

    FakeHmi_SetReady(true);
    GrayCheck_Update(1100u);                    /* 下周期缓存仍空 → 整行重试成功 */
    TEST_ASSERT_ROW(0u, "L:#...........");
    TEST_ASSERT_ROW(3u, "X:001 N:01");
    TEST_ASSERT_TRUE(FakeHmi_GetRowPrintCount(0u) == 1u);
    printf("PASS: test_panel_retry_after_not_ready\n");
    return 0;
}

/* 重进条目 = 统计清零 + 缓存失效全重绘（现场清零手势：BACK→ENTER）。 */
static int test_panel_restart_clears_stats(void)
{
    setup();
    FakeGrayPort_SetDarkChannels(0xFFFu);
    GrayCheck_Update(1000u);                    /* sticky=0xFFF */
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_ROW(1u, "S:############");

    GrayCheck_Stop();
    GrayCheck_Start();                          /* 重进：统计清零、行缓存失效 */
    FakeGrayPort_SetDarkChannels(0x000u);
    GrayCheck_Update(2000u);
    TEST_ASSERT_ROW(0u, "L:............");
    TEST_ASSERT_ROW(1u, "S:............");      /* 粘滞已清（旧值不残留） */
    TEST_ASSERT_ROW(2u, "T:............");
    TEST_ASSERT_ROW(3u, "X:000 N:00");
    printf("PASS: test_panel_restart_clears_stats\n");
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
    failures += test_panel_first_paint_rows();
    failures += test_panel_sticky_accumulates();
    failures += test_panel_toggle_count_saturates();
    failures += test_panel_row_diff_no_redraw();
    failures += test_panel_hundred_ms_gating();
    failures += test_panel_retry_after_not_ready();
    failures += test_panel_restart_clears_stats();

    if (failures != 0) {
        printf("gray_check service tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll gray_check service tests passed.\n");
    return 0;
}
