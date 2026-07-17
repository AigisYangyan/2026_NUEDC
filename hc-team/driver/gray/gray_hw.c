/**
 * @file    gray_hw.c
 * @brief   12 路灰度 Driver 的真实硬件实现（本模块唯一允许包含 TI 头的文件）
 *
 * 引脚一律经 board.syscfg 生成的 GPIO_LINE_SENSOR_PIN_IN*_PIN 宏取得，
 * **不得手抄引脚号**。手抄会让本文件与唯一配置源脱钩：改 syscfg 时这里不会报错，
 * 只会静默读错脚。
 *
 * 12 路必须同端口，这一约束由 board.syscfg 保证并在其注释中说明（IN4 特意占 PB8
 * 而非跨端口的 PA7）。若哪天 12 路被拆到两个端口，GPIO_LINE_SENSOR_PORT 组级宏会
 * 消失，本文件会编译失败 —— 那是刻意的：与其静默退化成多次读取，不如构建失败。
 */
#include "driver/gray/gray.h"
#include "driver/gray/gray_port.h"

#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_gpio.h>

/* 通道 -> 端口位掩码。索引即 gray.h 里的 bit 号：s_channel_pin[0] 对应 bit0 = IN1。
 * 顺序与 board.syscfg 的 associatedPins[0..11] 一一对应。 */
static const uint32_t s_channel_pin[GRAY_CHANNEL_COUNT] = {
    GPIO_LINE_SENSOR_PIN_IN1_PIN,
    GPIO_LINE_SENSOR_PIN_IN2_PIN,
    GPIO_LINE_SENSOR_PIN_IN3_PIN,
    GPIO_LINE_SENSOR_PIN_IN4_PIN,
    GPIO_LINE_SENSOR_PIN_IN5_PIN,
    GPIO_LINE_SENSOR_PIN_IN6_PIN,
    GPIO_LINE_SENSOR_PIN_IN7_PIN,
    GPIO_LINE_SENSOR_PIN_IN8_PIN,
    GPIO_LINE_SENSOR_PIN_IN9_PIN,
    GPIO_LINE_SENSOR_PIN_IN10_PIN,
    GPIO_LINE_SENSOR_PIN_IN11_PIN,
    GPIO_LINE_SENSOR_PIN_IN12_PIN,
};

/* 12 路的合并掩码。读端口时只取这 12 位，端口上其余引脚（属别的外设）与灰度无关。 */
#define GRAY_HW_ALL_PINS_MASK                                                  \
    (GPIO_LINE_SENSOR_PIN_IN1_PIN | GPIO_LINE_SENSOR_PIN_IN2_PIN |             \
     GPIO_LINE_SENSOR_PIN_IN3_PIN | GPIO_LINE_SENSOR_PIN_IN4_PIN |             \
     GPIO_LINE_SENSOR_PIN_IN5_PIN | GPIO_LINE_SENSOR_PIN_IN6_PIN |             \
     GPIO_LINE_SENSOR_PIN_IN7_PIN | GPIO_LINE_SENSOR_PIN_IN8_PIN |             \
     GPIO_LINE_SENSOR_PIN_IN9_PIN | GPIO_LINE_SENSOR_PIN_IN10_PIN |            \
     GPIO_LINE_SENSOR_PIN_IN11_PIN | GPIO_LINE_SENSOR_PIN_IN12_PIN)

uint32_t gray_port_read(void)
{
    /* ★ 全模块唯一一次端口读取。12 路同端口 -> 一次读全，路间零时间偏斜。 */
    return DL_GPIO_readPins(GPIO_LINE_SENSOR_PORT, GRAY_HW_ALL_PINS_MASK);
}

uint32_t gray_port_channel_mask(uint32_t channel)
{
    if (channel >= GRAY_CHANNEL_COUNT) {
        return 0u;
    }

    return s_channel_pin[channel];
}
