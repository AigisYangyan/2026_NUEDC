/**
 * @file    test_gray.c
 * @brief   12 路灰度 Driver 主机测试（P9.T1）
 *
 * 覆盖 Driver 的两条核心性质：
 * 1. 一次 Gray_ReadDarkBitmap() 只做一次端口读取（12 路原子采样）
 * 2. 通道 -> 端口位 的散射忠实于 gray_port_channel_mask()，不做恒等映射假设
 */
#include "driver/gray/gray.h"
#include <stdint.h>
#include <stdio.h>

extern void FakeGrayPort_Reset(void);
extern void FakeGrayPort_SetRaw(uint32_t raw);
extern void FakeGrayPort_SetDarkChannels(uint16_t channel_bitmap);
extern int FakeGrayPort_GetReadCount(void);
extern uint32_t FakeGrayPort_ChannelBit(uint32_t channel);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 全部 12 路照到深色 -> 12 位全 1。 */
static int test_all_dark_sets_all_channels(void)
{
    FakeGrayPort_Reset();
    FakeGrayPort_SetDarkChannels(0x0FFFu);

    TEST_ASSERT(Gray_ReadDarkBitmap() == 0x0FFFu);

    printf("PASS: test_all_dark_sets_all_channels\n");
    return 0;
}

/* 全部 12 路照到浅色 -> 位图为 0。 */
static int test_all_light_clears_all_channels(void)
{
    FakeGrayPort_Reset();
    FakeGrayPort_SetDarkChannels(0x0000u);

    TEST_ASSERT(Gray_ReadDarkBitmap() == 0x0000u);

    printf("PASS: test_all_light_clears_all_channels\n");
    return 0;
}

/* 逐通道单独置位：证明每一路都能独立落到自己的 bit 上，无串位。 */
static int test_each_channel_maps_to_its_own_bit(void)
{
    uint32_t channel = 0u;

    for (channel = 0u; channel < GRAY_CHANNEL_COUNT; channel++) {
        FakeGrayPort_Reset();
        FakeGrayPort_SetDarkChannels((uint16_t)(1u << channel));

        TEST_ASSERT(Gray_ReadDarkBitmap() == (uint16_t)(1u << channel));
    }

    printf("PASS: test_each_channel_maps_to_its_own_bit\n");
    return 0;
}

/* ★ 核心用例：散射必须走 gray_port_channel_mask()，不得假设 channel i == 端口 bit i。
 * 直接按真实接线（IN1=PB27）注入原始端口值：若 gray.c 用了恒等映射，
 * bit27 会被当成越界位丢掉，位图为 0，用例失败。 */
static int test_scatter_uses_port_mask_not_identity(void)
{
    FakeGrayPort_Reset();
    /* IN1 接 PB27：端口 bit27 为高，应落到位图 bit0。 */
    FakeGrayPort_SetRaw(1u << 27);
    TEST_ASSERT(Gray_ReadDarkBitmap() == 0x0001u);

    FakeGrayPort_Reset();
    /* IN12 接 PB24：端口 bit24 为高，应落到位图 bit11。 */
    FakeGrayPort_SetRaw(1u << 24);
    TEST_ASSERT(Gray_ReadDarkBitmap() == (uint16_t)(1u << 11));

    FakeGrayPort_Reset();
    /* IN11 接 PB0：端口 bit0 为高，应落到位图 bit10 —— 而不是 bit0。 */
    FakeGrayPort_SetRaw(1u << 0);
    TEST_ASSERT(Gray_ReadDarkBitmap() == (uint16_t)(1u << 10));

    printf("PASS: test_scatter_uses_port_mask_not_identity\n");
    return 0;
}

/* 端口上的非灰度位（属别的外设）不得渗进位图。 */
static int test_unrelated_port_bits_are_ignored(void)
{
    uint32_t unrelated = 0u;
    uint32_t channel = 0u;
    uint32_t used = 0u;

    for (channel = 0u; channel < GRAY_CHANNEL_COUNT; channel++) {
        used |= (1u << FakeGrayPort_ChannelBit(channel));
    }
    unrelated = ~used;

    FakeGrayPort_Reset();
    FakeGrayPort_SetRaw(unrelated);

    TEST_ASSERT(Gray_ReadDarkBitmap() == 0x0000u);

    printf("PASS: test_unrelated_port_bits_are_ignored\n");
    return 0;
}

/* ★ 原子采样守卫：一次调用只读一次端口。
 * board.syscfg 让 IN4 占 PB8（而非跨端口的 PA7）就是为了买到这个性质；
 * 旧的 app/tasks/track_follow/track_follow.c 是 12 次分读，从未兑现。
 * 若有人把读取挪进循环，本用例立刻失败。 */
static int test_single_port_read_per_call(void)
{
    FakeGrayPort_Reset();
    FakeGrayPort_SetDarkChannels(0x0A5Au);

    (void)Gray_ReadDarkBitmap();
    TEST_ASSERT(FakeGrayPort_GetReadCount() == 1);

    (void)Gray_ReadDarkBitmap();
    TEST_ASSERT(FakeGrayPort_GetReadCount() == 2);

    printf("PASS: test_single_port_read_per_call\n");
    return 0;
}

/* 高 4 位恒为 0：位图只有低 GRAY_CHANNEL_COUNT 位有效。 */
static int test_high_bits_always_zero(void)
{
    FakeGrayPort_Reset();
    FakeGrayPort_SetRaw(0xFFFFFFFFu);

    TEST_ASSERT(Gray_ReadDarkBitmap() == GRAY_BITMAP_MASK);
    TEST_ASSERT((Gray_ReadDarkBitmap() & (uint16_t)~GRAY_BITMAP_MASK) == 0u);

    printf("PASS: test_high_bits_always_zero\n");
    return 0;
}

/* Driver 无状态：同一端口值连读两次结果必须一致（无缓存、无迟滞、无去抖）。
 * 这条钉死「去抖归上层」的裁定 —— 一旦有人在 Driver 里加滤波，两次结果会不同。 */
static int test_stateless_repeated_read_is_identical(void)
{
    uint16_t first = 0u;
    uint16_t second = 0u;

    FakeGrayPort_Reset();
    FakeGrayPort_SetDarkChannels(0x0060u);

    first = Gray_ReadDarkBitmap();
    second = Gray_ReadDarkBitmap();
    TEST_ASSERT(first == second);
    TEST_ASSERT(first == 0x0060u);

    /* 立刻翻转输入，下一次读必须马上跟随，不得被任何滤波拖住。 */
    FakeGrayPort_SetDarkChannels(0x0801u);
    TEST_ASSERT(Gray_ReadDarkBitmap() == 0x0801u);

    printf("PASS: test_stateless_repeated_read_is_identical\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_all_dark_sets_all_channels();
    failures += test_all_light_clears_all_channels();
    failures += test_each_channel_maps_to_its_own_bit();
    failures += test_scatter_uses_port_mask_not_identity();
    failures += test_unrelated_port_bits_are_ignored();
    failures += test_single_port_read_per_call();
    failures += test_high_bits_always_zero();
    failures += test_stateless_repeated_read_is_identical();

    if (failures == 0) {
        printf("\nAll gray tests passed.\n");
        return 0;
    }

    printf("\n%d gray test(s) failed.\n", failures);
    return 1;
}
