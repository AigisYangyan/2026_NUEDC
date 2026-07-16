/**
 * @file    oled_hardware_i2c.c
 * @brief   OLED 硬件 I2C 显示驱动实现
 *
 * OLED 在 P6 之后独占 I2C_AUX；旧竞争器件已移除，因此这里不再引入额外总线
 * 抽象层。页寻址、字模、总线恢复和有界等待均保持为驱动私有细节。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/clock/clock.h"
#include "oled_hardware_i2c.h"

#if defined(HOST_TEST)
#define OLED_CPUCLK_HZ 80000000u
#define OLED_I2C_STATUS_BUSY  0x01u
#define OLED_I2C_STATUS_ERROR 0x02u

static const unsigned char asc2_0806[95][6] = {{0}};
static const unsigned char asc2_1608[95][16] = {{0}};

extern uint32_t FakeI2cPort_GetControllerStatus(void);
extern bool FakeI2cPort_IsControllerTxFifoFull(void);
extern void FakeI2cPort_TransmitControllerData(uint8_t data);
extern void FakeI2cPort_StartControllerTransfer(uint8_t addr7, uint16_t len);
extern void FakeI2cPort_DisableController(void);
extern void FakeI2cPort_EnableController(void);
#else
#include "ti_msp_dl_config.h"
#include "oledfont.h"

#include <ti/driverlib/dl_i2c.h>

#define OLED_CPUCLK_HZ CPUCLK_FREQ
#define OLED_I2C_STATUS_BUSY  DL_I2C_CONTROLLER_STATUS_BUSY
#define OLED_I2C_STATUS_ERROR DL_I2C_CONTROLLER_STATUS_ERROR
#endif

#define OLED_CMD   0u
#define OLED_DATA  1u

#define OLED_I2C_ADDR              0x3Cu
#define OLED_WIDTH                 128u
#define OLED_PAGE_COUNT            8u
#define OLED_POWER_STABLE_DELAY_MS 200u
#define OLED_ASCII_FIRST           ((uint8_t)' ')
#define OLED_ASCII_LAST            ((uint8_t)'~')

#define OLED_I2C_FAST_HZ              400000u
#define OLED_I2C_PACKET_BYTES         2u
#define OLED_I2C_BITS_PER_BYTE        9u
#define OLED_I2C_TIMEOUT_SAFETY       2u
#define OLED_POLL_LOOP_MIN_CYCLES     4u
#define OLED_I2C_PACKET_BITS \
    (OLED_I2C_PACKET_BYTES * OLED_I2C_BITS_PER_BYTE)
#define OLED_I2C_PACKET_TIME_US \
    (((OLED_I2C_PACKET_BITS * 1000000u) + (OLED_I2C_FAST_HZ - 1u)) / \
     OLED_I2C_FAST_HZ)
#define OLED_I2C_TIMEOUT_US \
    (OLED_I2C_PACKET_TIME_US * OLED_I2C_TIMEOUT_SAFETY)

/* board.syscfg 固定 I2C_AUX=Fast 400 kHz，生成配置 CPUCLK=80 MHz。
 * 单次 OLED 事务固定为 2 字节包（control + payload）：
 * 2 byte * 9 bit / 400 kHz = 45 us；安全系数 2 => 90 us。
 * Clock_NowMs() 仅有 ms 级粒度，不足以表达该上限，因此保留有界轮询：
 * timeout_loops = 90 us * 80 MHz / 4 cycles-per-poll(min) = 1800 loops。 */
#define OLED_I2C_TIMEOUT_LOOPS \
    (((OLED_I2C_TIMEOUT_US * (OLED_CPUCLK_HZ / 1000000u)) + \
      (OLED_POLL_LOOP_MIN_CYCLES - 1u)) / OLED_POLL_LOOP_MIN_CYCLES)

typedef enum {
    OLED_INIT_STATE_IDLE = 0,
    OLED_INIT_STATE_WAIT_POWER_STABLE,
    OLED_INIT_STATE_SEND_SEQUENCE,
    OLED_INIT_STATE_READY
} OLED_InitState_e;

typedef struct {
    uint8_t dev_addr;
} OLED_Bus_t;

typedef struct {
    OLED_Bus_t bus;
} OLED_Device_t;

static const uint8_t s_oled_init_cmds[] = {
    0xAEu, 0x00u, 0x10u, 0x40u, 0x81u, 0xCFu, 0xA1u, 0xC8u,
    0xA6u, 0xA8u, 0x3Fu, 0xD3u, 0x00u, 0xD5u, 0x80u, 0xD9u,
    0xF1u, 0xDAu, 0x12u, 0xDBu, 0x40u, 0x20u, 0x02u, 0x8Du,
    0x14u, 0xA4u, 0xA6u
};

static OLED_Device_t s_oled = {{OLED_I2C_ADDR}};
static OLED_InitState_e s_oled_init_state = OLED_INIT_STATE_IDLE;
static uint32_t s_oled_init_start_tick_ms = 0u;

static uint32_t oled_i2c_get_status(void)
{
#if defined(HOST_TEST)
    return FakeI2cPort_GetControllerStatus();
#else
    return DL_I2C_getControllerStatus(I2C_AUX_INST);
#endif
}

static bool oled_i2c_is_tx_fifo_full(void)
{
#if defined(HOST_TEST)
    return FakeI2cPort_IsControllerTxFifoFull();
#else
    return DL_I2C_isControllerTXFIFOFull(I2C_AUX_INST);
#endif
}

static void oled_i2c_transmit(uint8_t data)
{
#if defined(HOST_TEST)
    FakeI2cPort_TransmitControllerData(data);
#else
    DL_I2C_transmitControllerData(I2C_AUX_INST, data);
#endif
}

static void oled_i2c_start_transfer(uint8_t addr7, uint16_t len)
{
#if defined(HOST_TEST)
    FakeI2cPort_StartControllerTransfer(addr7, len);
#else
    DL_I2C_startControllerTransfer(I2C_AUX_INST,
                                   (uint32_t)addr7,
                                   DL_I2C_CONTROLLER_DIRECTION_TX,
                                   len);
#endif
}

static void oled_i2c_disable_controller(void)
{
#if defined(HOST_TEST)
    FakeI2cPort_DisableController();
#else
    DL_I2C_disableController(I2C_AUX_INST);
#endif
}

static void oled_i2c_enable_controller(void)
{
#if defined(HOST_TEST)
    FakeI2cPort_EnableController();
#else
    DL_I2C_enableController(I2C_AUX_INST);
#endif
}

static bool oled_is_ready_state(void)
{
    return (s_oled_init_state == OLED_INIT_STATE_READY) ? true : false;
}

static bool oled_delay_elapsed(uint32_t start_tick_ms,
                               uint32_t delay_ms,
                               uint32_t current_tick_ms)
{
    return ((current_tick_ms - start_tick_ms) >= delay_ms) ? true : false;
}

static Oled_Status_e oled_i2c_wait_not_busy(void)
{
    uint32_t timeout = OLED_I2C_TIMEOUT_LOOPS;

    while ((oled_i2c_get_status() & OLED_I2C_STATUS_BUSY) != 0u) {
        if (timeout == 0u) {
            return OLED_ERR_TIMEOUT;
        }

        timeout--;
    }

    return OLED_OK;
}

static Oled_Status_e oled_bus_recover(void)
{
    oled_i2c_disable_controller();
    oled_i2c_enable_controller();
    return OLED_OK;
}

static Oled_Status_e oled_i2c_master_write(uint8_t addr7,
                                           const uint8_t *p_data,
                                           uint16_t len)
{
    uint16_t index = 0u;
    Oled_Status_e wait_err = OLED_OK;

    if ((p_data == NULL) || (len == 0u)) {
        return OLED_ERR_NULL_PTR;
    }

    wait_err = oled_i2c_wait_not_busy();
    if (wait_err != OLED_OK) {
        return wait_err;
    }

    while ((index < len) && (oled_i2c_is_tx_fifo_full() == false)) {
        oled_i2c_transmit(p_data[index]);
        index++;
    }

    oled_i2c_start_transfer(addr7, len);

    while (index < len) {
        uint32_t timeout_fifo = OLED_I2C_TIMEOUT_LOOPS;

        while (oled_i2c_is_tx_fifo_full() != false) {
            if (timeout_fifo == 0u) {
                return OLED_ERR_TIMEOUT;
            }

            timeout_fifo--;
        }

        oled_i2c_transmit(p_data[index]);
        index++;
    }

    wait_err = oled_i2c_wait_not_busy();
    if (wait_err != OLED_OK) {
        return wait_err;
    }

    if ((oled_i2c_get_status() & OLED_I2C_STATUS_ERROR) != 0u) {
        return OLED_ERR_UNKNOWN;
    }

    return OLED_OK;
}

static Oled_Status_e oled_write_packet(uint8_t dat, uint8_t mode)
{
    uint8_t packet[2];
    Oled_Status_e ret = OLED_OK;

    packet[0] = (mode == OLED_DATA) ? 0x40u : 0x00u;
    packet[1] = dat;

    ret = oled_i2c_master_write(s_oled.bus.dev_addr, packet,
                                (uint16_t)sizeof(packet));
    if (ret != OLED_OK) {
        (void)oled_bus_recover();
        ret = oled_i2c_master_write(s_oled.bus.dev_addr, packet,
                                    (uint16_t)sizeof(packet));
    }

    return ret;
}

static Oled_Status_e oled_set_pos(uint8_t x, uint8_t y)
{
    Oled_Status_e ret = OLED_OK;

    ret = oled_write_packet((uint8_t)(0xB0u + y), OLED_CMD);
    if (ret != OLED_OK) {
        return ret;
    }

    ret = oled_write_packet((uint8_t)(((x & 0xF0u) >> 4) | 0x10u), OLED_CMD);
    if (ret != OLED_OK) {
        return ret;
    }

    return oled_write_packet((uint8_t)(x & 0x0Fu), OLED_CMD);
}

static bool oled_is_supported_size(uint8_t sizey)
{
    return ((sizey == 8u) || (sizey == 16u)) ? true : false;
}

static bool oled_is_supported_ascii(uint8_t chr)
{
    return ((chr >= OLED_ASCII_FIRST) && (chr <= OLED_ASCII_LAST)) ? true : false;
}

static bool oled_char_fits(uint8_t x, uint8_t y, uint8_t sizey)
{
    uint8_t width = (uint8_t)(sizey / 2u);
    uint8_t pages = (sizey == 8u) ? 1u : 2u;

    if ((x >= OLED_WIDTH) || (y >= OLED_PAGE_COUNT)) {
        return false;
    }

    if ((uint16_t)x + width > OLED_WIDTH) {
        return false;
    }

    if ((uint16_t)y + pages > OLED_PAGE_COUNT) {
        return false;
    }

    return true;
}

static Oled_Status_e oled_send_init_sequence(void)
{
    uint16_t index = 0u;
    Oled_Status_e ret = OLED_OK;

    for (index = 0u; index < (uint16_t)sizeof(s_oled_init_cmds); index++) {
        ret = oled_write_packet(s_oled_init_cmds[index], OLED_CMD);
        if (ret != OLED_OK) {
            return ret;
        }
    }

    ret = OLED_Clear();
    if (ret != OLED_OK) {
        return ret;
    }

    return oled_write_packet(0xAFu, OLED_CMD);
}

void OLED_Init(void)
{
    s_oled.bus.dev_addr = OLED_I2C_ADDR;
    (void)oled_bus_recover();
    s_oled_init_start_tick_ms = Clock_NowMs();
    s_oled_init_state = OLED_INIT_STATE_WAIT_POWER_STABLE;
}

Oled_Status_e OLED_Clear(void)
{
    uint8_t page = 0u;
    uint8_t column = 0u;
    Oled_Status_e ret = OLED_OK;

    for (page = 0u; page < OLED_PAGE_COUNT; page++) {
        ret = oled_set_pos(0u, page);
        if (ret != OLED_OK) {
            return ret;
        }

        for (column = 0u; column < OLED_WIDTH; column++) {
            ret = oled_write_packet(0x00u, OLED_DATA);
            if (ret != OLED_OK) {
                return ret;
            }
        }
    }

    return OLED_OK;
}

Oled_Status_e OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t sizey)
{
    uint8_t glyph_index = 0u;
    uint8_t glyph_width = 0u;
    uint16_t glyph_bytes = 0u;
    uint16_t index = 0u;
    Oled_Status_e ret = OLED_OK;

    if ((oled_is_supported_size(sizey) == false) ||
        (oled_is_supported_ascii(chr) == false) ||
        (oled_char_fits(x, y, sizey) == false)) {
        return OLED_ERR_INVALID;
    }

    glyph_index = (uint8_t)(chr - OLED_ASCII_FIRST);
    glyph_width = (uint8_t)(sizey / 2u);
    glyph_bytes = (sizey == 8u) ? 6u : 16u;

    ret = oled_set_pos(x, y);
    if (ret != OLED_OK) {
        return ret;
    }

    for (index = 0u; index < glyph_bytes; index++) {
        if ((sizey == 16u) && ((index % glyph_width) == 0u) && (index != 0u)) {
            ret = oled_set_pos(x, (uint8_t)(y + 1u));
            if (ret != OLED_OK) {
                return ret;
            }
        }

        if (sizey == 8u) {
            ret = oled_write_packet(asc2_0806[glyph_index][index], OLED_DATA);
        }
        else {
            ret = oled_write_packet(asc2_1608[glyph_index][index], OLED_DATA);
        }

        if (ret != OLED_OK) {
            return ret;
        }
    }

    return OLED_OK;
}

Oled_Status_e OLED_ShowString(uint8_t x, uint8_t y, const char *chr,
                              uint8_t sizey)
{
    uint8_t step = 0u;
    Oled_Status_e ret = OLED_OK;

    if (chr == NULL) {
        return OLED_ERR_NULL_PTR;
    }

    if (oled_is_supported_size(sizey) == false) {
        return OLED_ERR_INVALID;
    }

    step = (sizey == 8u) ? 6u : (uint8_t)(sizey / 2u);
    while (*chr != '\0') {
        ret = OLED_ShowChar(x, y, (uint8_t)(*chr), sizey);
        if (ret != OLED_OK) {
            return ret;
        }

        x = (uint8_t)(x + step);
        chr++;
    }

    return OLED_OK;
}

Oled_Status_e OLED_Process(void)
{
    Oled_Status_e ret = OLED_OK;

    if (s_oled_init_state == OLED_INIT_STATE_IDLE) {
        return OLED_OK;
    }

    if (s_oled_init_state == OLED_INIT_STATE_WAIT_POWER_STABLE) {
        if (oled_delay_elapsed(s_oled_init_start_tick_ms,
                               OLED_POWER_STABLE_DELAY_MS,
                               Clock_NowMs()) == false) {
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
