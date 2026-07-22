/**
 * @file    uart_vision.c
 * @brief   视觉链路协议编解码 Driver 实现
 *
 * 自同步分帧：首字节区分两条协议——
 * - 0xAA → 坐标控制帧（0xAA 0x55 + len + payload + CRC16-MODBUS）
 * - 0xFF → 选题/握手帧（0xFF + 主任务 + 子任务 + 0xFE）
 * 坏字节/坏 CRC 丢一字节重扫；长度前缀 + len 白名单 + CRC 使分帧确定性自恢复，
 * 故无逐字节超时、不依赖 Clock。字节层原始收发由 vision_uart 负责。
 */
#include "driver/uart_vision/uart_vision.h"

#include "driver/board_uart/vision_uart.h"

#include <stddef.h>
#include <string.h>

#define UART_VISION_HDR0             0xAAu   /* 坐标帧同步字节 0 */
#define UART_VISION_HDR1             0x55u   /* 坐标帧同步字节 1 */
#define UART_VISION_HS_LEAD          0xFFu   /* 握手帧起始 */
#define UART_VISION_HS_TAIL          0xFEu   /* 握手帧结束 */
#define UART_VISION_HS_FRAME_LEN     4u      /* 0xFF main sub 0xFE */

#define UART_VISION_CMD_COORD        0x01u   /* payload[0]：目标坐标 */
#define UART_VISION_COORD_PAYLOAD_LEN 9u     /* cmd(1) + x(4) + y(4) */
#define UART_VISION_CMD_STATUS       0x02u   /* payload[0]：目标状态（V1 §36） */
#define UART_VISION_STATUS_PAYLOAD_LEN 3u    /* cmd(1) + 状态位域(2) */

#define UART_VISION_MAX_PAYLOAD_LEN  16u     /* len 白名单上界，超界即噪声→重扫 */
/* 最长帧 = 2(hdr) + 1(len) + MAX_PAYLOAD + 2(crc) = 21；缓冲留裕量。 */
#define UART_VISION_RX_BUF_SIZE      32u
#define UART_VISION_READ_CHUNK_SIZE  32u

typedef struct {
    uint8_t data[UART_VISION_RX_BUF_SIZE];
    uint8_t count;

    UartVision_Coord_T coord;
    uint32_t coord_seq;

    uint8_t ack_main;
    uint8_t ack_sub;
    uint32_t ack_seq;

    uint8_t status[2];
    uint32_t status_seq;
} UartVision_State_t;

static UartVision_State_t s_uart_vision;

/* CRC16-MODBUS：init 0xFFFF，反射多项式 0xA001；范围由调用方给定。 */
static uint16_t uart_vision_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i = 0u;
    uint8_t bit = 0u;

    for (i = 0u; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }

    return crc;
}

static void uart_vision_drop_prefix(uint8_t drop_count)
{
    if (drop_count >= s_uart_vision.count) {
        s_uart_vision.count = 0u;
        return;
    }

    memmove(s_uart_vision.data,
            &s_uart_vision.data[drop_count],
            (size_t)(s_uart_vision.count - drop_count));
    s_uart_vision.count = (uint8_t)(s_uart_vision.count - drop_count);
}

static void uart_vision_store_coord(const uint8_t *payload)
{
    UartVision_Coord_T coord;

    memcpy(&coord.x, &payload[1], sizeof(float));
    memcpy(&coord.y, &payload[5], sizeof(float));

    s_uart_vision.coord = coord;
    s_uart_vision.coord_seq++;
}

static bool uart_vision_try_consume_frame(void)
{
    uint8_t len = 0u;
    uint8_t frame_len = 0u;
    uint16_t crc_calc = 0u;
    uint16_t crc_rx = 0u;

    while (s_uart_vision.count > 0u) {
        uint8_t lead = s_uart_vision.data[0];

        if (lead == UART_VISION_HDR0) {
            if (s_uart_vision.count < 2u) {
                return false;
            }
            if (s_uart_vision.data[1] != UART_VISION_HDR1) {
                uart_vision_drop_prefix(1u);
                continue;
            }
            if (s_uart_vision.count < 3u) {
                return false;
            }
            len = s_uart_vision.data[2];
            if ((len == 0u) || (len > UART_VISION_MAX_PAYLOAD_LEN)) {
                uart_vision_drop_prefix(1u);
                continue;
            }
            frame_len = (uint8_t)(len + 5u);
            if (s_uart_vision.count < frame_len) {
                return false;
            }

            crc_calc = uart_vision_crc16(&s_uart_vision.data[2],
                                         (uint16_t)(1u + len));
            crc_rx = (uint16_t)((uint16_t)s_uart_vision.data[3u + len] |
                                ((uint16_t)s_uart_vision.data[4u + len] << 8));
            if (crc_calc != crc_rx) {
                uart_vision_drop_prefix(1u);
                continue;
            }

            /* 校验通过：坐标/状态帧各刷新各的缓存；未知 cmd 静默丢弃。 */
            if ((s_uart_vision.data[3] == UART_VISION_CMD_COORD) &&
                (len == UART_VISION_COORD_PAYLOAD_LEN)) {
                uart_vision_store_coord(&s_uart_vision.data[3]);
            } else if ((s_uart_vision.data[3] == UART_VISION_CMD_STATUS) &&
                       (len == UART_VISION_STATUS_PAYLOAD_LEN)) {
                s_uart_vision.status[0] = s_uart_vision.data[4];
                s_uart_vision.status[1] = s_uart_vision.data[5];
                s_uart_vision.status_seq++;
            }
            uart_vision_drop_prefix(frame_len);
            return true;
        } else if (lead == UART_VISION_HS_LEAD) {
            if (s_uart_vision.count < UART_VISION_HS_FRAME_LEN) {
                return false;
            }
            if (s_uart_vision.data[3] != UART_VISION_HS_TAIL) {
                uart_vision_drop_prefix(1u);
                continue;
            }
            s_uart_vision.ack_main = s_uart_vision.data[1];
            s_uart_vision.ack_sub = s_uart_vision.data[2];
            s_uart_vision.ack_seq++;
            uart_vision_drop_prefix(UART_VISION_HS_FRAME_LEN);
            return true;
        } else {
            uart_vision_drop_prefix(1u);
            continue;
        }
    }

    return false;
}

static void uart_vision_process_rx_byte(uint8_t byte)
{
    if (s_uart_vision.count >= UART_VISION_RX_BUF_SIZE) {
        uart_vision_drop_prefix(1u);
    }

    s_uart_vision.data[s_uart_vision.count++] = byte;

    while (uart_vision_try_consume_frame() == true) {
    }
}

void UartVision_Init(void)
{
    memset(&s_uart_vision, 0, sizeof(s_uart_vision));
    VisionUart_Init();
}

void UartVision_Poll(void)
{
    uint8_t read_buf[UART_VISION_READ_CHUNK_SIZE];
    uint32_t read_count = 0u;
    uint32_t index = 0u;

    do {
        read_count = VisionUart_Read(read_buf, sizeof(read_buf));
        for (index = 0u; index < read_count; index++) {
            uart_vision_process_rx_byte(read_buf[index]);
        }
    } while (read_count > 0u);
}

bool UartVision_GetLatestCoord(UartVision_Coord_T *out)
{
    if ((out == NULL) || (s_uart_vision.coord_seq == 0u)) {
        return false;
    }

    *out = s_uart_vision.coord;
    return true;
}

uint32_t UartVision_GetCoordSeq(void)
{
    return s_uart_vision.coord_seq;
}

bool UartVision_GetLatestStatus(uint8_t out[2])
{
    if ((out == NULL) || (s_uart_vision.status_seq == 0u)) {
        return false;
    }
    out[0] = s_uart_vision.status[0];
    out[1] = s_uart_vision.status[1];
    return true;
}

uint32_t UartVision_GetStatusSeq(void)
{
    return s_uart_vision.status_seq;
}

bool UartVision_SendTopic(uint8_t main_task, uint8_t sub_task)
{
    uint8_t frame[UART_VISION_HS_FRAME_LEN];

    frame[0] = UART_VISION_HS_LEAD;
    frame[1] = main_task;
    frame[2] = sub_task;
    frame[3] = UART_VISION_HS_TAIL;

    return VisionUart_TryWrite(frame, (uint32_t)sizeof(frame));
}

bool UartVision_GetTopicAck(uint8_t *main_task, uint8_t *sub_task)
{
    if (s_uart_vision.ack_seq == 0u) {
        return false;
    }

    if (main_task != NULL) {
        *main_task = s_uart_vision.ack_main;
    }
    if (sub_task != NULL) {
        *sub_task = s_uart_vision.ack_sub;
    }
    return true;
}

uint32_t UartVision_GetTopicAckSeq(void)
{
    return s_uart_vision.ack_seq;
}
