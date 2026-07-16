/**
 * @file    oled_hardware_i2c.c
 * @brief   OLED 硬件 I2C 驱动模块实现
 *
 * 本文件实现 128x64 OLED 的基础显示驱动，作为显示执行层（I2C + SSD1306 页寻址）。
 *
 * 功能范围：
 * - 发送 OLED 命令与显示数据
 * - 设置页地址、清屏、开关显示、旋转/反色控制
 * - 基于现有字库显示数字、英文、中文与位图
 *
 * 不负责的内容：
 * - 菜单逻辑、页面刷新策略与业务排版
 * - 显存缓存、脏矩形刷新等高级显示优化
 * - 字库生成与中文字模维护
 *
 * 实现说明：
 * 1. 使用“全局实例 + Init 填充硬件绑定”方式管理 OLED 的 I2C 资源
 * 2. 底层 I2C 走 SysConfig I2C_AUX_INST + 阻塞 DriverLib 事务
 * 3. 通信异常时先执行一次 I2C 总线恢复，再重试当前发送
 *
 * 硬件绑定项：
 * - I2C_AUX_INST (I2C1, 400 kHz)
 * - OLED 7-bit 设备地址 0x3C
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "oled_hardware_i2c.h"
#include "driver/clock/clock.h"
#include "oledfont.h"
#include "ti_msp_dl_config.h"

#include <ti/driverlib/dl_i2c.h>

/* ---- 静态配置与全局实例 ------------------------------------------------- */

#define OLED_I2C_ADDR   0x3Cu
#define OLED_WIDTH      128u
#define OLED_PAGE_COUNT 8u
#define OLED_POWER_STABLE_DELAY_MS 200u
#define OLED_I2C_TIMEOUT_LOOPS     50000u

typedef enum {
    OLED_INIT_STATE_IDLE = 0,
    OLED_INIT_STATE_WAIT_POWER_STABLE,
    OLED_INIT_STATE_SEND_SEQUENCE,
    OLED_INIT_STATE_READY
} OLED_InitState_e;

static const OLED_Bus_T s_tOledDefaultBus = {
    OLED_I2C_ADDR
};

static const uint8_t s_oled_init_cmds[] = {
    0xAEu, 0x00u, 0x10u, 0x40u, 0x81u, 0xCFu, 0xA1u, 0xC8u,
    0xA6u, 0xA8u, 0x3Fu, 0xD3u, 0x00u, 0xD5u, 0x80u, 0xD9u,
    0xF1u, 0xDAu, 0x12u, 0xDBu, 0x40u, 0x20u, 0x02u, 0x8Du,
    0x14u, 0xA4u, 0xA6u
};

OLED_T g_tOLED = {
    { OLED_I2C_ADDR }
};

static OLED_InitState_e s_oled_init_state = OLED_INIT_STATE_IDLE;
static uint32_t s_oled_init_start_tick_ms = 0u;

/* ---- 静态辅助函数 ------------------------------------------------------- */

static bool oled_is_ready_state(void)
{
    return (s_oled_init_state == OLED_INIT_STATE_READY) ? true : false;
}

static Oled_Status_e oled_i2c_wait_not_busy(void)
{
    uint32_t timeout = OLED_I2C_TIMEOUT_LOOPS;

    while ((DL_I2C_getControllerStatus(I2C_AUX_INST) &
            DL_I2C_CONTROLLER_STATUS_BUSY) != 0u) {
        if (timeout == 0u) {
            return OLED_ERR_TIMEOUT;
        }
        timeout--;
    }
    return OLED_OK;
}

static Oled_Status_e oled_bus_recover(void)
{
    DL_I2C_disableController(I2C_AUX_INST);
    DL_I2C_enableController(I2C_AUX_INST);
    return OLED_OK;
}

static Oled_Status_e oled_i2c_master_write(uint8_t addr7,
                                        const uint8_t *p_data,
                                        uint16_t len)
{
    uint16_t i = 0u;
    uint32_t timeout_fifo;
    Oled_Status_e wait_err;

    if ((p_data == NULL) || (len == 0u)) {
        return OLED_ERR_NULL_PTR;
    }

    wait_err = oled_i2c_wait_not_busy();
    if (wait_err != OLED_OK) {
        return wait_err;
    }

    while ((i < len) &&
           (DL_I2C_isControllerTXFIFOFull(I2C_AUX_INST) == false)) {
        DL_I2C_transmitControllerData(I2C_AUX_INST, p_data[i]);
        i++;
    }

    DL_I2C_startControllerTransfer(I2C_AUX_INST,
                                   (uint32_t)addr7,
                                   DL_I2C_CONTROLLER_DIRECTION_TX,
                                   len);

    while (i < len) {
        timeout_fifo = OLED_I2C_TIMEOUT_LOOPS;
        while (DL_I2C_isControllerTXFIFOFull(I2C_AUX_INST) != false) {
            if (timeout_fifo == 0u) {
                return OLED_ERR_TIMEOUT;
            }
            timeout_fifo--;
        }
        DL_I2C_transmitControllerData(I2C_AUX_INST, p_data[i]);
        i++;
    }

    wait_err = oled_i2c_wait_not_busy();
    if (wait_err != OLED_OK) {
        return wait_err;
    }

    if ((DL_I2C_getControllerStatus(I2C_AUX_INST) &
         DL_I2C_CONTROLLER_STATUS_ERROR) != 0u) {
        return OLED_ERR_UNKNOWN;
    }

    return OLED_OK;
}

static Oled_Status_e oled_write_packet(const OLED_T *p_oled, uint8_t dat,
                                    uint8_t mode)
{
    uint8_t packet[2];
    Oled_Status_e ret;

    packet[0] = (mode == OLED_DATA) ? 0x40u : 0x00u;
    packet[1] = dat;

    ret = oled_i2c_master_write(p_oled->bus.dev_addr, packet,
                                (uint16_t)sizeof(packet));
    if (ret != OLED_OK) {
        (void)oled_bus_recover();
        ret = oled_i2c_master_write(p_oled->bus.dev_addr, packet,
                                    (uint16_t)sizeof(packet));
    }

    return ret;
}

static bool oled_delay_elapsed(uint32_t start_tick_ms, uint32_t delay_ms,
                                    uint32_t current_tick_ms)
{
    return ((current_tick_ms - start_tick_ms) >= delay_ms) ? true : false;
}

static Oled_Status_e oled_send_init_sequence(void)
{
    uint16_t i;
    Oled_Status_e ret;

    for (i = 0u; i < (uint16_t)sizeof(s_oled_init_cmds); i++) {
        ret = OLED_WR_Byte(s_oled_init_cmds[i], OLED_CMD);
        if (ret != OLED_OK) {
            return ret;
        }
    }

    ret = OLED_Clear();
    if (ret != OLED_OK) {
        return ret;
    }

    return OLED_WR_Byte(0xAFu, OLED_CMD);
}

/* ---- 公开 API ----------------------------------------------------------- */

void oled_i2c_sda_unlock(void)
{
    (void)oled_bus_recover();
}

void OLED_ColorTurn(uint8_t enable_inverse)
{
    if (enable_inverse == 0u) {
        (void)OLED_WR_Byte(0xA6u, OLED_CMD);
    }
    else {
        (void)OLED_WR_Byte(0xA7u, OLED_CMD);
    }
}

void OLED_DisplayTurn(uint8_t enable_rotate)
{
    if (enable_rotate == 0u) {
        (void)OLED_WR_Byte(0xC8u, OLED_CMD);
        (void)OLED_WR_Byte(0xA1u, OLED_CMD);
    }
    else {
        (void)OLED_WR_Byte(0xC0u, OLED_CMD);
        (void)OLED_WR_Byte(0xA0u, OLED_CMD);
    }
}

Oled_Status_e OLED_WR_Byte(uint8_t dat, uint8_t mode)
{
    return oled_write_packet(&g_tOLED, dat, mode);
}

void OLED_Set_Pos(uint8_t x, uint8_t y)
{
    (void)OLED_WR_Byte((uint8_t)(0xB0u + y), OLED_CMD);
    (void)OLED_WR_Byte((uint8_t)(((x & 0xF0u) >> 4) | 0x10u), OLED_CMD);
    (void)OLED_WR_Byte((uint8_t)(x & 0x0Fu), OLED_CMD);
}

void OLED_Display_On(void)
{
    (void)OLED_WR_Byte(0x8Du, OLED_CMD);
    (void)OLED_WR_Byte(0x14u, OLED_CMD);
    (void)OLED_WR_Byte(0xAFu, OLED_CMD);
}

void OLED_Display_Off(void)
{
    (void)OLED_WR_Byte(0x8Du, OLED_CMD);
    (void)OLED_WR_Byte(0x10u, OLED_CMD);
    (void)OLED_WR_Byte(0xAEu, OLED_CMD);
}

Oled_Status_e OLED_Clear(void)
{
    uint8_t page;
    uint8_t column;
    Oled_Status_e ret;

    for (page = 0u; page < OLED_PAGE_COUNT; page++) {
        ret = OLED_WR_Byte((uint8_t)(0xB0u + page), OLED_CMD);
        if (ret != OLED_OK) {
            return ret;
        }

        ret = OLED_WR_Byte(0x00u, OLED_CMD);
        if (ret != OLED_OK) {
            return ret;
        }

        ret = OLED_WR_Byte(0x10u, OLED_CMD);
        if (ret != OLED_OK) {
            return ret;
        }

        for (column = 0u; column < OLED_WIDTH; column++) {
            ret = OLED_WR_Byte(0x00u, OLED_DATA);
            if (ret != OLED_OK) {
                return ret;
            }
        }
    }

    return OLED_OK;
}

Oled_Status_e OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t sizey)
{
    uint8_t c = (uint8_t)(chr - ' ');
    uint8_t sizex = (uint8_t)(sizey / 2u);
    uint16_t i;
    uint16_t size1;
    Oled_Status_e ret;

    if (sizey == 8u) {
        size1 = 6u;
    }
    else {
        size1 = (uint16_t)((sizey / 8u + ((sizey % 8u) ? 1u : 0u)) *
                           (sizey / 2u));
    }

    OLED_Set_Pos(x, y);
    for (i = 0u; i < size1; i++) {
        if (((i % sizex) == 0u) && (sizey != 8u)) {
            OLED_Set_Pos(x, y++);
        }

        if (sizey == 8u) {
            ret = OLED_WR_Byte(asc2_0806[c][i], OLED_DATA);
        }
        else if (sizey == 16u) {
            ret = OLED_WR_Byte(asc2_1608[c][i], OLED_DATA);
        }
        else {
            return OLED_ERR_INVALID;
        }

        if (ret != OLED_OK) {
            return ret;
        }
    }

    return OLED_OK;
}

uint32_t oled_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1u;

    while (n-- != 0u) {
        result *= m;
    }

    return result;
}

Oled_Status_e OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len,
                        uint8_t sizey)
{
    uint8_t t;
    uint8_t temp;
    uint8_t m = 0u;
    uint8_t enshow = 0u;
    Oled_Status_e ret;

    if (sizey == 8u) {
        m = 2u;
    }

    for (t = 0u; t < len; t++) {
        temp = (uint8_t)((num / oled_pow(10u, (uint8_t)(len - t - 1u))) % 10u);
        if ((enshow == 0u) && (t < (len - 1u))) {
            if (temp == 0u) {
                ret = OLED_ShowChar((uint8_t)(x + (sizey / 2u + m) * t), y,
                                    ' ', sizey);
                if (ret != OLED_OK) {
                    return ret;
                }
                continue;
            }
            enshow = 1u;
        }

        ret = OLED_ShowChar((uint8_t)(x + (sizey / 2u + m) * t), y,
                            (uint8_t)(temp + '0'), sizey);
        if (ret != OLED_OK) {
            return ret;
        }
    }

    return OLED_OK;
}

Oled_Status_e OLED_ShowString(uint8_t x, uint8_t y, const char *chr,
                           uint8_t sizey)
{
    uint8_t j = 0u;
    Oled_Status_e ret;

    if (chr == NULL) {
        return OLED_ERR_NULL_PTR;
    }

    while (chr[j] != '\0') {
        ret = OLED_ShowChar(x, y, (uint8_t)chr[j++], sizey);
        if (ret != OLED_OK) {
            return ret;
        }

        if (sizey == 8u) {
            x = (uint8_t)(x + 6u);
        }
        else {
            x = (uint8_t)(x + sizey / 2u);
        }
    }

    return OLED_OK;
}

Oled_Status_e OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t no, uint8_t sizey)
{
    uint16_t i;
    Oled_Status_e ret;
    const uint16_t size1 =
        (uint16_t)((sizey / 8u + ((sizey % 8u) ? 1u : 0u)) * sizey);

    for (i = 0u; i < size1; i++) {
        if ((i % sizey) == 0u) {
            OLED_Set_Pos(x, y++);
        }

        if (sizey == 16u) {
            ret = OLED_WR_Byte(Hzk[no][i], OLED_DATA);
        }
        else {
            return OLED_ERR_INVALID;
        }

        if (ret != OLED_OK) {
            return ret;
        }
    }

    return OLED_OK;
}

void OLED_DrawBMP(uint8_t x, uint8_t y, uint8_t sizex, uint8_t sizey,
                  const uint8_t BMP[])
{
    uint16_t j = 0u;
    uint8_t page;
    uint8_t column;

    sizey = (uint8_t)(sizey / 8u + ((sizey % 8u) ? 1u : 0u));
    for (page = 0u; page < sizey; page++) {
        OLED_Set_Pos(x, (uint8_t)(page + y));
        for (column = 0u; column < sizex; column++) {
            (void)OLED_WR_Byte(BMP[j++], OLED_DATA);
        }
    }
}

void OLED_Init(void)
{
    g_tOLED.bus = s_tOledDefaultBus;
    oled_i2c_sda_unlock();

    s_oled_init_start_tick_ms = Clock_NowMs();
    s_oled_init_state = OLED_INIT_STATE_WAIT_POWER_STABLE;
}

Oled_Status_e OLED_Process(void)
{
    uint32_t tick_ms = 0u;
    Oled_Status_e ret;

    if (s_oled_init_state == OLED_INIT_STATE_IDLE) {
        return OLED_OK;
    }

    if (s_oled_init_state == OLED_INIT_STATE_WAIT_POWER_STABLE) {
        tick_ms = Clock_NowMs();

        if (oled_delay_elapsed(s_oled_init_start_tick_ms,
                               OLED_POWER_STABLE_DELAY_MS,
                               tick_ms) == false) {
            return OLED_OK;
        }

        s_oled_init_state = OLED_INIT_STATE_SEND_SEQUENCE;
    }

    if (s_oled_init_state == OLED_INIT_STATE_SEND_SEQUENCE) {
        ret = oled_send_init_sequence();
        if (ret != OLED_OK) {
            return ret;
        }

        s_oled_init_state = OLED_INIT_STATE_READY;
    }

    return OLED_OK;
}

bool OLED_IsReady(void)
{
    return oled_is_ready_state();
}
