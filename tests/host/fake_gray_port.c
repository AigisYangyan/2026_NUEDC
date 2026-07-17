/**
 * @file    fake_gray_port.c
 * @brief   gray_port.h 的主机假件，顶替 gray_hw.c。
 *
 * 让主机测试能注入任意端口原始值，并观测 gray.c 对端口读取的调用次数
 * （12 路原子采样性质的守卫）。
 *
 * ★ 本假件的掩码表照抄 board.syscfg 的真实接线，目的是让测试自带接线文档、
 *   且天然是散乱映射（防止 gray.c 用 channel==bit 的恒等映射蒙混过关）。
 *   但它**不能证明**真实表正确 —— 真实表在 gray_hw.c 里全部由 syscfg 生成宏
 *   构成（P9.T1 证据行 E04 钉死「零手抄引脚号」），靠的是构造上无从抄错，
 *   而不是靠本假件。
 */
#include "driver/gray/gray.h"
#include "driver/gray/gray_port.h"

/* 与 board.syscfg 的 GPIO_LINE_SENSOR 组一致：
 * IN1=PB27 IN2=PB12 IN3=PB13 IN4=PB8  IN5=PB20 IN6=PB26
 * IN7=PB17 IN8=PB19 IN9=PB21 IN10=PB14 IN11=PB0 IN12=PB24 */
static const uint32_t s_fake_channel_bit[GRAY_CHANNEL_COUNT] = {
    27u, 12u, 13u, 8u, 20u, 26u, 17u, 19u, 21u, 14u, 0u, 24u,
};

static uint32_t s_fake_raw;
static int s_read_count;

void FakeGrayPort_Reset(void)
{
    s_fake_raw = 0u;
    s_read_count = 0;
}

/** 直接设定端口原始值（含非灰度位，用于验证掩码）。 */
void FakeGrayPort_SetRaw(uint32_t raw)
{
    s_fake_raw = raw;
}

/** 按通道号设定「深色」，内部转成对应端口位。 */
void FakeGrayPort_SetDarkChannels(uint16_t channel_bitmap)
{
    uint32_t channel = 0u;

    s_fake_raw = 0u;
    for (channel = 0u; channel < GRAY_CHANNEL_COUNT; channel++) {
        if ((channel_bitmap & (1u << channel)) != 0u) {
            s_fake_raw |= (1u << s_fake_channel_bit[channel]);
        }
    }
}

int FakeGrayPort_GetReadCount(void)
{
    return s_read_count;
}

uint32_t FakeGrayPort_ChannelBit(uint32_t channel)
{
    return s_fake_channel_bit[channel];
}

uint32_t gray_port_read(void)
{
    s_read_count++;
    return s_fake_raw;
}

uint32_t gray_port_channel_mask(uint32_t channel)
{
    if (channel >= GRAY_CHANNEL_COUNT) {
        return 0u;
    }

    return (1u << s_fake_channel_bit[channel]);
}
