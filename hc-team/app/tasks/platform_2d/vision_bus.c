/**
 * @file    vision_bus.c
 * @brief   视觉坐标串口总线服务模块实现
 *
 * 本文件实现 UART_VISION 上视觉协议的任务态拉取与分帧。
 * 原始字节由 VisionUart Driver 以 FIFO 方式缓存；本模块只保留
 * 最小分帧状态，不再持有 App 侧原始 RX FIFO。
 */

#include "app/tasks/platform_2d/vision_bus.h"

#include "app/tasks/platform_2d/vision_coord.h"
#include "driver/board_uart/vision_uart.h"
#include "driver/clock/clock.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define VISION_BUS_READ_CHUNK_SIZE      32u
#define VISION_BUS_INTERBYTE_TIMEOUT_MS 20u
#define VISION_BUS_HEADER_0             0x55u
#define VISION_BUS_HEADER_1             0xAAu
#define VISION_BUS_FRAME_BUFFER_SIZE    VISION_COORD_TARGET_FRAME_LEN

typedef struct {
    uint8_t data[VISION_BUS_FRAME_BUFFER_SIZE];
    uint8_t count;
    uint32_t last_rx_tick;
} VisionBus_RxState_t;

static VisionBus_RxState_t s_vision_bus_rx_state;

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

static uint8_t vision_bus_expected_frame_len(uint8_t payload_len)
{
    return (uint8_t)(VISION_COORD_FRAME_MIN_LEN + payload_len);
}

static void vision_bus_drop_prefix(uint8_t drop_count)
{
    if (drop_count >= s_vision_bus_rx_state.count) {
        s_vision_bus_rx_state.count = 0u;
        return;
    }

    memmove(s_vision_bus_rx_state.data,
            &s_vision_bus_rx_state.data[drop_count],
            (size_t)(s_vision_bus_rx_state.count - drop_count));
    s_vision_bus_rx_state.count =
        (uint8_t)(s_vision_bus_rx_state.count - drop_count);
}

static bool vision_bus_try_consume_frame(void)
{
    uint8_t cmd = 0u;
    uint8_t payload_len = 0u;
    uint8_t frame_len = 0u;

    while (s_vision_bus_rx_state.count > 0u) {
        if (s_vision_bus_rx_state.data[0] != VISION_BUS_HEADER_0) {
            vision_bus_drop_prefix(1u);
            continue;
        }

        if (s_vision_bus_rx_state.count < 2u) {
            return false;
        }
        if (s_vision_bus_rx_state.data[1] != VISION_BUS_HEADER_1) {
            vision_bus_drop_prefix(1u);
            continue;
        }

        if (s_vision_bus_rx_state.count < 3u) {
            return false;
        }
        cmd = s_vision_bus_rx_state.data[2];
        if (vision_bus_is_cmd(cmd) == false) {
            vision_bus_drop_prefix(1u);
            continue;
        }

        if (s_vision_bus_rx_state.count < 4u) {
            return false;
        }
        payload_len = s_vision_bus_rx_state.data[3];
        if (vision_bus_is_payload_len(cmd, payload_len) == false) {
            vision_bus_drop_prefix(1u);
            continue;
        }

        frame_len = vision_bus_expected_frame_len(payload_len);
        if (s_vision_bus_rx_state.count < frame_len) {
            return false;
        }

        (void)VisionCoord_HandleFrame(s_vision_bus_rx_state.data, frame_len);
        vision_bus_drop_prefix(frame_len);
        return true;
    }

    return false;
}

static void vision_bus_process_rx_byte(uint8_t data)
{
    if (s_vision_bus_rx_state.count >= VISION_BUS_FRAME_BUFFER_SIZE) {
        vision_bus_drop_prefix(1u);
    }

    s_vision_bus_rx_state.data[s_vision_bus_rx_state.count++] = data;
    s_vision_bus_rx_state.last_rx_tick = Clock_NowMs();

    while (vision_bus_try_consume_frame() == true) {
    }
}

static void vision_bus_discard_stale_partial(void)
{
    uint32_t now_ms = 0u;

    if (s_vision_bus_rx_state.count == 0u) {
        return;
    }

    now_ms = Clock_NowMs();
    if ((now_ms - s_vision_bus_rx_state.last_rx_tick) >=
        VISION_BUS_INTERBYTE_TIMEOUT_MS) {
        s_vision_bus_rx_state.count = 0u;
    }
}

void VisionBus_Init(void)
{
    memset(&s_vision_bus_rx_state, 0, sizeof(s_vision_bus_rx_state));
    VisionCoord_Init();
}

void VisionBus_Service5ms(void)
{
    uint8_t read_buf[VISION_BUS_READ_CHUNK_SIZE];
    uint32_t read_count = 0u;
    uint32_t index = 0u;

    vision_bus_discard_stale_partial();

    do {
        read_count = VisionUart_Read(read_buf, sizeof(read_buf));
        for (index = 0u; index < read_count; index++) {
            vision_bus_process_rx_byte(read_buf[index]);
        }
    } while (read_count > 0u);

    vision_bus_discard_stale_partial();
}
