/**
 * @file    uart_wireless.c
 * @brief   无线链路 Driver 实现：自同步分帧状态机 + 最新帧信箱（单一所有者）。
 *
 * CRC16-MODBUS 为本驱动对本字节流的私有校验实现（与 uart_vision 的算法同款但
 * 各校验各的流，不构成第二数据变换所有者）。
 */
#include "driver/uart_wireless/uart_wireless.h"

#include "driver/board_uart/wireless_uart.h"

#define WL_HEAD0 0xA5u
#define WL_HEAD1 0x5Au
#define WL_TYPE_USER      0x01u
#define WL_TYPE_HEARTBEAT 0x02u

typedef enum {
    WL_HUNT0 = 0, WL_HUNT1, WL_LEN, WL_TYPE, WL_PAYLOAD, WL_CRC_L, WL_CRC_H
} Wl_ParseState_T;

static Wl_ParseState_T s_state;
static uint8_t  s_len;
static uint8_t  s_type;
static uint8_t  s_payload[WIRELESS_MAX_PAYLOAD];
static uint8_t  s_payload_idx;
static uint16_t s_crc_lo;

static uint8_t  s_mail[WIRELESS_MAX_PAYLOAD];
static uint8_t  s_mail_len;
static bool     s_mail_fresh;

static Wireless_Diag_T s_diag;

/** CRC16-MODBUS（0xA001 反射多项式，初值 0xFFFF）。 */
static uint16_t wl_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint8_t bit;

    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/** 帧内 CRC 的增量口径：len+type+payload 拼一段算（帧短，直接重算不增量）。 */
static uint16_t wl_frame_crc(uint8_t len, uint8_t type, const uint8_t *payload)
{
    uint8_t buf[2u + WIRELESS_MAX_PAYLOAD];

    buf[0] = len;
    buf[1] = type;
    if (len > 0u) {
        uint8_t i;
        for (i = 0u; i < len; i++) {
            buf[2u + i] = payload[i];
        }
    }
    return wl_crc16(buf, (uint32_t)len + 2u);
}

static void wl_accept_frame(void)
{
    s_diag.frame_count++;
    if (s_type == WL_TYPE_USER) {
        uint8_t i;
        for (i = 0u; i < s_len; i++) {
            s_mail[i] = s_payload[i];
        }
        s_mail_len = s_len;         /* 后到覆盖先到（最新帧信箱） */
        s_mail_fresh = true;
    }
    /* 心跳帧只计数（活性判定归 link 服务）。 */
}

static void wl_feed(uint8_t byte)
{
    switch (s_state) {
    case WL_HUNT0:
        if (byte == WL_HEAD0) {
            s_state = WL_HUNT1;
        }
        break;
    case WL_HUNT1:
        s_state = (byte == WL_HEAD1) ? WL_LEN : WL_HUNT0;
        break;
    case WL_LEN:
        if (byte > WIRELESS_MAX_PAYLOAD) {
            s_diag.crc_error_count++;   /* len 越界与 CRC 错同归拒收计数 */
            s_state = WL_HUNT0;
        } else {
            s_len = byte;
            s_payload_idx = 0u;
            s_state = WL_TYPE;
        }
        break;
    case WL_TYPE:
        s_type = byte;
        s_state = (s_len > 0u) ? WL_PAYLOAD : WL_CRC_L;
        break;
    case WL_PAYLOAD:
        s_payload[s_payload_idx++] = byte;
        if (s_payload_idx >= s_len) {
            s_state = WL_CRC_L;
        }
        break;
    case WL_CRC_L:
        s_crc_lo = byte;
        s_state = WL_CRC_H;
        break;
    case WL_CRC_H: {
        uint16_t rx_crc = (uint16_t)(s_crc_lo | ((uint16_t)byte << 8));
        if (rx_crc == wl_frame_crc(s_len, s_type, s_payload)) {
            wl_accept_frame();
        } else {
            s_diag.crc_error_count++;
        }
        s_state = WL_HUNT0;         /* 自同步：无论成败回猎头 */
        break;
    }
    default:
        s_state = WL_HUNT0;
        break;
    }
}

static bool wl_send_frame(uint8_t type, const uint8_t *data, uint8_t len)
{
    uint8_t frame[4u + WIRELESS_MAX_PAYLOAD + 2u];
    uint16_t crc;
    uint8_t i;

    if (s_diag.port_absent || (len > WIRELESS_MAX_PAYLOAD)) {
        return false;
    }
    frame[0] = WL_HEAD0;
    frame[1] = WL_HEAD1;
    frame[2] = len;
    frame[3] = type;
    for (i = 0u; i < len; i++) {
        frame[4u + i] = data[i];
    }
    crc = wl_frame_crc(len, type, data);
    frame[4u + len] = (uint8_t)(crc & 0xFFu);
    frame[5u + len] = (uint8_t)(crc >> 8);
    return WirelessUart_Write(frame, (uint32_t)len + 6u);
}

void Wireless_Init(void)
{
    s_state = WL_HUNT0;
    s_len = 0u;
    s_type = 0u;
    s_payload_idx = 0u;
    s_mail_len = 0u;
    s_mail_fresh = false;
    s_diag.frame_count = 0u;
    s_diag.crc_error_count = 0u;
    s_diag.rx_overflows = 0u;
    s_diag.port_absent = !WirelessUart_Init();
}

void Wireless_Poll(void)
{
    uint8_t chunk[32];
    uint32_t n;
    uint32_t i;

    if (s_diag.port_absent) {
        return;
    }
    for (;;) {
        n = WirelessUart_Read(chunk, sizeof(chunk));
        if (n == 0u) {
            break;
        }
        for (i = 0u; i < n; i++) {
            wl_feed(chunk[i]);
        }
    }
}

bool Wireless_SendUser(const uint8_t *data, uint8_t len)
{
    if (data == (void *)0) {
        return false;
    }
    return wl_send_frame(WL_TYPE_USER, data, len);
}

bool Wireless_SendHeartbeat(void)
{
    static const uint8_t none = 0u;

    return wl_send_frame(WL_TYPE_HEARTBEAT, &none, 0u);
}

bool Wireless_TakeLatestUser(uint8_t *buf, uint8_t cap, uint8_t *len_out)
{
    uint8_t i;

    if (!s_mail_fresh || (buf == (void *)0) || (len_out == (void *)0) || (cap < s_mail_len)) {
        return false;
    }
    for (i = 0u; i < s_mail_len; i++) {
        buf[i] = s_mail[i];
    }
    *len_out = s_mail_len;
    s_mail_fresh = false;           /* 一次性消费 */
    return true;
}

uint32_t Wireless_RxFrameCount(void)
{
    return s_diag.frame_count;
}

void Wireless_GetDiag(Wireless_Diag_T *out)
{
    if (out == (void *)0) {
        return;
    }
    s_diag.rx_overflows = WirelessUart_GetRxOverflowCount();    /* 端口是唯一计数者 */
    *out = s_diag;
}
