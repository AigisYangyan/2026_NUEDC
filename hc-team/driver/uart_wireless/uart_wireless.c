/**
 * @file    uart_wireless.c
 * @brief   无线链路 Driver 实现 v2：自同步分帧 + 双投递语义（单一所有者）。
 *
 * seq 所有权：不可靠流（STATE+心跳）与事件流各一个 TX 计数器，唯一递增点在本文件；
 * gap/dup/去重判定同为本文件唯一所有者——上层不复做。CRC16-MODBUS 为本驱动对本
 * 字节流的私有校验实现（与 uart_vision 同款算法、各校验各的流）。
 */
#include "driver/uart_wireless/uart_wireless.h"

#include "driver/board_uart/wireless_uart.h"

#define WL_HEAD0 0xA5u
#define WL_HEAD1 0x5Au
#define WL_TYPE_STATE     0x01u
#define WL_TYPE_HEARTBEAT 0x02u
#define WL_TYPE_EVENT     0x03u
#define WL_TYPE_ACK       0x04u

/* 事件低频（指令级），深 4 = stop-and-wait 重试窗口内全部可能在途量。 */
#define WL_EVENT_QUEUE_DEPTH 4u

typedef enum {
    WL_HUNT0 = 0, WL_HUNT1, WL_LEN, WL_TYPE, WL_SEQ, WL_PAYLOAD, WL_CRC_L, WL_CRC_H
} Wl_ParseState_T;

static Wl_ParseState_T s_state;
static uint8_t  s_len;
static uint8_t  s_type;
static uint8_t  s_seq;
static uint8_t  s_payload[WIRELESS_MAX_PAYLOAD];
static uint8_t  s_payload_idx;
static uint16_t s_crc_lo;

/* TX seq：不可靠流与事件流各自单调（mod 256）。 */
static uint8_t  s_tx_ur_seq;
static uint8_t  s_tx_ev_seq;

/* 本端在途事件（stop-and-wait 单槽）。 */
static bool     s_ev_pending;
static uint8_t  s_ev_pend_seq;
static uint8_t  s_ev_pend_data[WIRELESS_MAX_PAYLOAD];
static uint8_t  s_ev_pend_len;

/* RX 不可靠流账本 + 状态信箱。 */
static bool     s_ur_seeded;
static uint8_t  s_ur_last_seq;
static uint8_t  s_mail[WIRELESS_MAX_PAYLOAD];
static uint8_t  s_mail_len;
static bool     s_mail_fresh;

/* RX 事件流：去重账本 + FIFO 队列（全收不漏）。 */
static bool     s_ev_rx_seeded;
static uint8_t  s_ev_rx_last_seq;
typedef struct {
    uint8_t len;
    uint8_t data[WIRELESS_MAX_PAYLOAD];
} Wl_Event_T;
static Wl_Event_T s_evq[WL_EVENT_QUEUE_DEPTH];
static uint8_t  s_evq_head;
static uint8_t  s_evq_count;

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

/** 帧内 CRC 口径：len+type+seq+payload 拼一段算（帧短，直接重算不增量）。 */
static uint16_t wl_frame_crc(uint8_t len, uint8_t type, uint8_t seq, const uint8_t *payload)
{
    uint8_t buf[3u + WIRELESS_MAX_PAYLOAD];

    buf[0] = len;
    buf[1] = type;
    buf[2] = seq;
    if (len > 0u) {
        uint8_t i;
        for (i = 0u; i < len; i++) {
            buf[3u + i] = payload[i];
        }
    }
    return wl_crc16(buf, (uint32_t)len + 3u);
}

static bool wl_send_frame(uint8_t type, uint8_t seq, const uint8_t *data, uint8_t len)
{
    uint8_t frame[5u + WIRELESS_MAX_PAYLOAD + 2u];
    uint16_t crc;
    uint8_t i;

    if (s_diag.port_absent || (len > WIRELESS_MAX_PAYLOAD)) {
        return false;
    }
    frame[0] = WL_HEAD0;
    frame[1] = WL_HEAD1;
    frame[2] = len;
    frame[3] = type;
    frame[4] = seq;
    for (i = 0u; i < len; i++) {
        frame[5u + i] = data[i];
    }
    crc = wl_frame_crc(len, type, seq, data);
    frame[5u + len] = (uint8_t)(crc & 0xFFu);
    frame[6u + len] = (uint8_t)(crc >> 8);
    if (!WirelessUart_Write(frame, (uint32_t)len + 7u)) {
        s_diag.tx_fail_count++;
        return false;
    }
    return true;
}

/** 不可靠流（STATE/心跳共用）RX 账：gap/dup。返回 false=重复帧应排除。 */
static bool wl_ur_account(uint8_t seq)
{
    if (!s_ur_seeded) {
        s_ur_seeded = true;
        s_ur_last_seq = seq;
        return true;
    }
    if (seq == s_ur_last_seq) {
        s_diag.ur_dup_count++;
        return false;
    }
    s_diag.ur_gap_count += (uint32_t)((uint8_t)(seq - s_ur_last_seq - 1u));
    s_ur_last_seq = seq;
    return true;
}

static void wl_accept_frame(void)
{
    switch (s_type) {
    case WL_TYPE_STATE:
        s_diag.frame_count++;
        if (wl_ur_account(s_seq)) {
            uint8_t i;
            for (i = 0u; i < s_len; i++) {
                s_mail[i] = s_payload[i];
            }
            s_mail_len = s_len;     /* 后到覆盖先到（最新帧信箱） */
            s_mail_fresh = true;
        }
        break;
    case WL_TYPE_HEARTBEAT:
        s_diag.frame_count++;
        (void)wl_ur_account(s_seq); /* 心跳只走账（丢包率探针），无载荷投递 */
        break;
    case WL_TYPE_EVENT:
        s_diag.frame_count++;
        if (s_ev_rx_seeded && (s_seq == s_ev_rx_last_seq)) {
            s_diag.ev_dup_count++;
            (void)wl_send_frame(WL_TYPE_ACK, s_seq, (const uint8_t *)0, 0u);   /* 重 ACK：对端 ACK 丢了 */
        } else if (s_evq_count >= WL_EVENT_QUEUE_DEPTH) {
            s_diag.ev_rx_drop_count++;  /* 满不 ACK：宁可让对端重传，不假确认 */
        } else {
            uint8_t slot = (uint8_t)((s_evq_head + s_evq_count) % WL_EVENT_QUEUE_DEPTH);
            uint8_t i;
            for (i = 0u; i < s_len; i++) {
                s_evq[slot].data[i] = s_payload[i];
            }
            s_evq[slot].len = s_len;
            s_evq_count++;
            s_ev_rx_seeded = true;
            s_ev_rx_last_seq = s_seq;
            (void)wl_send_frame(WL_TYPE_ACK, s_seq, (const uint8_t *)0, 0u);
        }
        break;
    case WL_TYPE_ACK:
        s_diag.frame_count++;
        if (s_ev_pending && (s_seq == s_ev_pend_seq)) {
            s_ev_pending = false;
            s_diag.delivered_count++;
        }
        /* 迟到/无主 ACK 忽略（stop-and-wait 下唯一合法 ACK 就是 pending seq）。 */
        break;
    default:
        s_diag.unknown_type_count++;    /* CRC 合法但协议失配：不计有效帧、不刷活性 */
        break;
    }
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
        s_state = WL_SEQ;
        break;
    case WL_SEQ:
        s_seq = byte;
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
        if (rx_crc == wl_frame_crc(s_len, s_type, s_seq, s_payload)) {
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

void Wireless_Init(void)
{
    s_state = WL_HUNT0;
    s_len = 0u;
    s_type = 0u;
    s_seq = 0u;
    s_payload_idx = 0u;
    s_tx_ur_seq = 0u;
    s_tx_ev_seq = 0u;
    s_ev_pending = false;
    s_ev_pend_seq = 0u;
    s_ev_pend_len = 0u;
    s_ur_seeded = false;
    s_ur_last_seq = 0u;
    s_mail_len = 0u;
    s_mail_fresh = false;
    s_ev_rx_seeded = false;
    s_ev_rx_last_seq = 0u;
    s_evq_head = 0u;
    s_evq_count = 0u;
    s_diag.frame_count = 0u;
    s_diag.crc_error_count = 0u;
    s_diag.unknown_type_count = 0u;
    s_diag.ur_gap_count = 0u;
    s_diag.ur_dup_count = 0u;
    s_diag.ev_rx_drop_count = 0u;
    s_diag.ev_dup_count = 0u;
    s_diag.retx_count = 0u;
    s_diag.delivered_count = 0u;
    s_diag.ev_fail_count = 0u;
    s_diag.tx_fail_count = 0u;
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

bool Wireless_SendState(const uint8_t *data, uint8_t len)
{
    if (data == (void *)0) {
        return false;
    }
    if (!wl_send_frame(WL_TYPE_STATE, s_tx_ur_seq, data, len)) {
        return false;
    }
    s_tx_ur_seq++;      /* 只有真正出端口的帧消耗 seq（gap 账才干净） */
    return true;
}

bool Wireless_SendHeartbeat(void)
{
    if (!wl_send_frame(WL_TYPE_HEARTBEAT, s_tx_ur_seq, (const uint8_t *)0, 0u)) {
        return false;
    }
    s_tx_ur_seq++;
    return true;
}

bool Wireless_SendEvent(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    if ((data == (void *)0) || s_ev_pending ||
        (len > WIRELESS_MAX_PAYLOAD) || s_diag.port_absent) {
        return false;
    }
    for (i = 0u; i < len; i++) {
        s_ev_pend_data[i] = data[i];
    }
    s_ev_pend_len = len;
    s_ev_pend_seq = s_tx_ev_seq;
    s_tx_ev_seq++;
    s_ev_pending = true;
    /* 首发失败不撤销 pending：重传机制兜底，tx_fail_count 已留痕。 */
    (void)wl_send_frame(WL_TYPE_EVENT, s_ev_pend_seq, s_ev_pend_data, s_ev_pend_len);
    return true;
}

bool Wireless_ResendEvent(void)
{
    if (!s_ev_pending) {
        return false;
    }
    s_diag.retx_count++;
    return wl_send_frame(WL_TYPE_EVENT, s_ev_pend_seq, s_ev_pend_data, s_ev_pend_len);
}

void Wireless_AbandonEvent(void)
{
    if (s_ev_pending) {
        s_ev_pending = false;
        s_diag.ev_fail_count++;
    }
}

bool Wireless_EventPending(void)
{
    return s_ev_pending;
}

bool Wireless_TakeLatestState(uint8_t *buf, uint8_t cap, uint8_t *len_out)
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

bool Wireless_TakeEvent(uint8_t *buf, uint8_t cap, uint8_t *len_out)
{
    uint8_t i;

    if ((s_evq_count == 0u) || (buf == (void *)0) || (len_out == (void *)0) ||
        (cap < s_evq[s_evq_head].len)) {
        return false;
    }
    for (i = 0u; i < s_evq[s_evq_head].len; i++) {
        buf[i] = s_evq[s_evq_head].data[i];
    }
    *len_out = s_evq[s_evq_head].len;
    s_evq_head = (uint8_t)((s_evq_head + 1u) % WL_EVENT_QUEUE_DEPTH);
    s_evq_count--;
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
