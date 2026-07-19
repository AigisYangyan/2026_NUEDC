/**
 * @file    gray_check.c
 * @brief   12 路灰度数字量遥测诊断服务实现：VOFA tx×12 只读遥测 + 10ms 自门控读/发帧。
 *
 * 数据链（§8.2）：
 *   GPIO 电平 → gray_hw 一次 DL_GPIO_readPins → gray.c 位散射 → Gray_ReadDarkBitmap
 *   [bit i = 通道 i 深色，器件比较器电平，无滤波/去抖/反相/阈值]
 *   → 本服务逐位镜像 12 个 tx（单向复制，零第二处理）→ vofa_run [uart_vofa：JustFloat 组帧发送]。
 *
 * 所有权：12 路原子读唯一在 gray driver；本服务只 (bitmap>>i)&1 复制，不反相、不去抖、不重排。
 */
#include "app/service/gray_check/gray_check.h"

#include "driver/gray/gray.h"
#include "driver/uart_vofa/uart_vofa.h"

#define GRAY_CHECK_STREAM_PERIOD_MS 10u

/* tx 遥测镜像（注册序 = 通道序 = 显示序）：
 * ch_i（上位机 G(i+1)）= Gray bit i 的深色数字量，1=深色（线上）、0=浅色。
 * 仅镜像原始通道序，不代表车上左右/上下（见 gray_check.h 位序说明）。 */
static int      s_tx_channel[GRAY_CHANNEL_COUNT];
static uint32_t s_period_base_ms;

static void gray_check_refresh_tx(void)
{
    const uint16_t bitmap = Gray_ReadDarkBitmap();
    uint32_t i;

    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        /* 单向复制：逐位提取，零反相/去抖/滤波/左右重排（§8.2 单一所有者）。 */
        s_tx_channel[i] = (((bitmap >> i) & 1u) != 0u) ? 1 : 0;
    }
}

void GrayCheck_Start(void)
{
    uint32_t i;

    vofa_clear_profile();

    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        s_tx_channel[i] = 0;
    }

    /* 注册序 = 通道序 ch0..ch11（G1..G12）；零 bind_cmd（只读诊断，不接收上位机命令）。 */
    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        (void)vofa_register_int(&s_tx_channel[i]);
    }

    /* base=0：gray 无 elapsed 消费者，无需播种拍——进页首拍即 now-0≥10 发一帧。 */
    s_period_base_ms = 0u;
}

void GrayCheck_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */

    if (elapsed_ms < GRAY_CHECK_STREAM_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    gray_check_refresh_tx();
    vofa_run(); /* RX 排空（零绑定 = 无副作用）+ 发本拍刚镜像的 12 路数字量帧（刷新在发送前，无一帧延迟） */
}

void GrayCheck_Stop(void)
{
    vofa_clear_profile();
}
