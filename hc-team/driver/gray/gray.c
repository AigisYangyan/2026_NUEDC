/**
 * @file    gray.c
 * @brief   12 路灰度阵列 Driver 实现：端口原始值 -> 通道深色位图
 *
 * 本文件不含任何 TI 头依赖，全部硬件访问经 gray_port.h 转出，因此可在主机上测试。
 *
 * 实现说明：
 * 1. 对 gray_port_read() 的调用**恰好一次**。12 路在同一端口，一次读取即取回全部，
 *    路间无时间偏斜。多读一次就会破坏这个性质，故不得把读取放进循环。
 * 2. 通道号 -> 端口位 的映射不在本文件里，由 gray_port_channel_mask() 提供。
 *    实际接线是散乱的（IN1=PB27、IN2=PB12、IN4=PB8 ...），本文件不作任何假设。
 */
#include "driver/gray/gray.h"
#include "driver/gray/gray_port.h"

uint16_t Gray_ReadDarkBitmap(void)
{
    /* 单次读取：该性质由 tests/host/test_gray.c 的调用计数用例钉死。 */
    const uint32_t raw = gray_port_read();
    uint16_t bitmap = 0u;
    uint32_t channel = 0u;

    for (channel = 0u; channel < GRAY_CHANNEL_COUNT; channel++) {
        /* 器件事实：深色背景 -> 该脚为高电平。 */
        if ((raw & gray_port_channel_mask(channel)) != 0u) {
            bitmap |= (uint16_t)(1u << channel);
        }
    }

    return bitmap;
}
