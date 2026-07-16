/**
 * @file    vision_bus.c
 * @brief   视觉坐标串口总线服务模块实现
 *
 * 本文件实现 UART_VISION (UART1) 上视觉协议的收包与分帧。
 *
 * 工作流程：
 * 1. UART RX ISR 把单字节推入环形 FIFO
 * 2. 5ms 周期任务中：找帧头、按 cmd/payload_len 计算整帧长度、拷贝出完整帧
 * 3. 把完整帧传给 VisionCoord_HandleFrame；FIFO 丢弃已消费字节
 * 4. 若前缀不合法则丢 1 字节继续搜索帧头
 * 5. 数据停滞超过 INTERBYTE_TIMEOUT_MS 且残留前缀非法则整体重置 FIFO
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app/tasks/platform_2d/vision_bus.h"

#include "ti_msp_dl_config.h"
#include "app/tasks/platform_2d/vision_coord.h"
#include "driver/clock/clock.h"
#include <string.h>

/* ---- 静态配置 ----------------------------------------------------------- */

#define VISION_BUS_RX_FIFO_SIZE          256u
#define VISION_BUS_RX_PROCESS_BUDGET     64u
#define VISION_BUS_INTERBYTE_TIMEOUT_MS  20u
#define VISION_BUS_HEADER_0              0x55u
#define VISION_BUS_HEADER_1              0xAAu

/* ---- 内部类型 ----------------------------------------------------------- */

typedef struct {
    uint8_t data[VISION_BUS_RX_FIFO_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    volatile uint32_t last_rx_tick;
} VisionBus_RxFifo_t;

/* ---- 模块状态 ----------------------------------------------------------- */

static VisionBus_RxFifo_t s_vision_rx_fifo;

/* ---- FIFO 基本操作 ------------------------------------------------------ */

static uint32_t vision_bus_irq_lock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void vision_bus_irq_unlock(uint32_t primask)
{
    if (primask == 0u) {
        __enable_irq();
    }
}

static void vision_bus_rx_reset(void)
{
    uint32_t primask = vision_bus_irq_lock();
    s_vision_rx_fifo.head = 0u;
    s_vision_rx_fifo.tail = 0u;
    s_vision_rx_fifo.count = 0u;
    vision_bus_irq_unlock(primask);
}

static uint16_t vision_bus_rx_count(void)
{
    return s_vision_rx_fifo.count;
}

static void vision_bus_rx_push(uint8_t data)
{
    if (s_vision_rx_fifo.count >= VISION_BUS_RX_FIFO_SIZE) {
        //FIFO 溢出：丢最旧字节腾空间
        s_vision_rx_fifo.tail =
            (uint16_t)((s_vision_rx_fifo.tail + 1u) % VISION_BUS_RX_FIFO_SIZE);
        s_vision_rx_fifo.count--;
    }

    s_vision_rx_fifo.data[s_vision_rx_fifo.head] = data;
    s_vision_rx_fifo.head =
        (uint16_t)((s_vision_rx_fifo.head + 1u) % VISION_BUS_RX_FIFO_SIZE);
    s_vision_rx_fifo.count++;

    s_vision_rx_fifo.last_rx_tick = Clock_NowMs();
}

static bool vision_bus_rx_peek(uint16_t offset, uint8_t *p_data)
{
    uint16_t index;

    if ((p_data == NULL) || (offset >= s_vision_rx_fifo.count)) {
        return false;
    }

    index = (uint16_t)((s_vision_rx_fifo.tail + offset) % VISION_BUS_RX_FIFO_SIZE);
    *p_data = s_vision_rx_fifo.data[index];
    return true;
}

static bool vision_bus_rx_copy(uint16_t length, uint8_t *p_buf)
{
    uint16_t i;

    if ((p_buf == NULL) || (length > s_vision_rx_fifo.count)) {
        return false;
    }

    for (i = 0u; i < length; i++) {
        if (vision_bus_rx_peek(i, &p_buf[i]) == false) {
            return false;
        }
    }
    return true;
}

static void vision_bus_rx_drop(uint16_t length)
{
    uint32_t primask = vision_bus_irq_lock();

    if (length >= s_vision_rx_fifo.count) {
        s_vision_rx_fifo.head = 0u;
        s_vision_rx_fifo.tail = 0u;
        s_vision_rx_fifo.count = 0u;
    }
    else {
        s_vision_rx_fifo.tail =
            (uint16_t)((s_vision_rx_fifo.tail + length) % VISION_BUS_RX_FIFO_SIZE);
        s_vision_rx_fifo.count = (uint16_t)(s_vision_rx_fifo.count - length);
    }

    vision_bus_irq_unlock(primask);
}

/* ---- 视觉协议判定 ------------------------------------------------------- */

static bool vision_bus_is_cmd(uint8_t cmd)
{
    return (bool)((cmd == VISION_COORD_FRAME_CMD_TOPIC) ||
                       (cmd == VISION_COORD_FRAME_CMD_TARGET) ||
                       (cmd == VISION_COORD_FRAME_CMD_LOST_TARGET));
}

static bool vision_bus_is_payload_len(uint8_t cmd, uint8_t payload_len)
{
    if (cmd == VISION_COORD_FRAME_CMD_TOPIC) {
        return (bool)((payload_len == VISION_COORD_TOPIC_PAYLOAD_LEN) ||
                           (payload_len == VISION_COORD_TOPIC_PAYLOAD_COMPAT_LEN));
    }
    if (cmd == VISION_COORD_FRAME_CMD_TARGET) {
        return (bool)(payload_len == VISION_COORD_TARGET_PAYLOAD_LEN);
    }
    if (cmd == VISION_COORD_FRAME_CMD_LOST_TARGET) {
        return (bool)(payload_len == VISION_COORD_LOST_TARGET_PAYLOAD_LEN);
    }
    return false;
}

static uint16_t vision_bus_get_frame_len(uint8_t payload_len)
{
    return (uint16_t)(VISION_COORD_FRAME_MIN_LEN + payload_len);
}

//尝试读取当前 FIFO 中完整帧长度，返回 FALSE 表示前缀不完整或非法
static bool vision_bus_try_get_frame_len(uint16_t *p_frame_len)
{
    uint8_t byte = 0u;
    uint8_t cmd = 0u;
    uint8_t payload_len = 0u;

    if ((p_frame_len == NULL) ||
        (vision_bus_rx_count() < VISION_COORD_FRAME_HEADER_LEN)) {
        return false;
    }

    if ((vision_bus_rx_peek(0u, &byte) == false) || (byte != VISION_BUS_HEADER_0) ||
        (vision_bus_rx_peek(1u, &byte) == false) || (byte != VISION_BUS_HEADER_1) ||
        (vision_bus_rx_peek(2u, &cmd) == false) ||
        (vision_bus_is_cmd(cmd) == false) ||
        (vision_bus_rx_peek(3u, &payload_len) == false) ||
        (vision_bus_is_payload_len(cmd, payload_len) == false)) {
        return false;
    }

    *p_frame_len = vision_bus_get_frame_len(payload_len);
    return true;
}

//判断 FIFO 是否持有一个“部分有效”的视觉帧前缀（尚未凑齐完整帧）
static bool vision_bus_has_partial_prefix(void)
{
    uint8_t byte = 0u;
    uint8_t cmd = 0u;
    uint8_t payload_len = 0u;
    uint16_t count = vision_bus_rx_count();

    if (count == 0u) {
        return false;
    }

    if ((vision_bus_rx_peek(0u, &byte) == false) || (byte != VISION_BUS_HEADER_0)) {
        return false;
    }
    if (count == 1u) {
        return true;
    }

    if ((vision_bus_rx_peek(1u, &byte) == false) || (byte != VISION_BUS_HEADER_1)) {
        return false;
    }
    if (count == 2u) {
        return true;
    }

    if ((vision_bus_rx_peek(2u, &cmd) == false) ||
        (vision_bus_is_cmd(cmd) == false)) {
        return false;
    }
    if (count == 3u) {
        return true;
    }

    if ((vision_bus_rx_peek(3u, &payload_len) == false) ||
        (vision_bus_is_payload_len(cmd, payload_len) == false)) {
        return false;
    }

    return (bool)(count < vision_bus_get_frame_len(payload_len));
}

static void vision_bus_discard_stale_partial(void)
{
    uint32_t tick_ms = 0u;

    if (s_vision_rx_fifo.count == 0u) {
        return;
    }

    tick_ms = Clock_NowMs();

    if ((tick_ms - s_vision_rx_fifo.last_rx_tick) < VISION_BUS_INTERBYTE_TIMEOUT_MS) {
        return;
    }

    //超时后若前缀非法，整体清空
    if (vision_bus_has_partial_prefix() == false) {
        vision_bus_rx_reset();
    }
}

//尝试消费一整帧；返回 TRUE 表示消费了一帧（不管上层是否接受）
static bool vision_bus_try_consume_frame(uint16_t *p_consumed_len)
{
    uint16_t frame_len = 0u;
    uint8_t frame[VISION_COORD_TARGET_FRAME_LEN];

    if (p_consumed_len != NULL) {
        *p_consumed_len = 0u;
    }

    if ((vision_bus_try_get_frame_len(&frame_len) == false) ||
        (vision_bus_rx_count() < frame_len)) {
        return false;
    }

    if (vision_bus_rx_copy(frame_len, frame) == false) {
        return false;
    }

    vision_bus_rx_drop(frame_len);
    if (p_consumed_len != NULL) {
        *p_consumed_len = frame_len;
    }

    (void)VisionCoord_HandleFrame(frame, frame_len);
    return true;
}

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 视觉总线初始化
 * @note  清空 FIFO 并初始化 VisionCoord 业务状态
 */
void VisionBus_Init(void)
{
    memset(&s_vision_rx_fifo, 0, sizeof(s_vision_rx_fifo));
    VisionCoord_Init();
}

/**
 * @brief 视觉 UART 接收 ISR 入口
 * @param data 本次接收到的单字节
 */
void VisionBus_RxISR(uint8_t data)
{
    vision_bus_rx_push(data);
}

/**
 * @brief 视觉总线 5ms 周期服务
 * @note  尝试消费 FIFO 内的完整视觉帧，必要时按字节搜索帧头
 */
void VisionBus_Service5ms(void)
{
    uint16_t processed = 0u;

    vision_bus_discard_stale_partial();

    while (processed < VISION_BUS_RX_PROCESS_BUDGET) {
        uint16_t consumed = 0u;

        if (vision_bus_rx_count() == 0u) {
            break;
        }

        if (vision_bus_try_consume_frame(&consumed) == true) {
            processed = (uint16_t)(processed + consumed);
            continue;
        }

        //前缀部分有效但未凑齐整帧，下次再试
        if (vision_bus_has_partial_prefix() == true) {
            break;
        }

        //前缀非法，丢 1 字节继续搜索帧头
        vision_bus_rx_drop(1u);
        processed++;
    }

    vision_bus_discard_stale_partial();
}
