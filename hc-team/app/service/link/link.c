/**
 * @file    link.c
 * @brief   无线链路服务实现：心跳/重试节拍与活性窗口唯一所有者。
 */
#include "app/service/link/link.h"

#include "driver/uart_wireless/uart_wireless.h"
#include "driver/uart_vofa/uart_vofa.h"

#define LINK_UPDATE_PERIOD_MS   10u
#define LINK_HEARTBEAT_MS       200u
#define LINK_ALIVE_WINDOW_MS    600u
/* 事件重传节拍：4 tick=40ms/次、上限 8 次（末次重发最迟 320ms 发出，放弃判定在
 * 第 9 拍=360ms）→ 最坏 360ms 内定胜负，仍在 600ms 活性窗口内。
 * 编译期常量：丢包率不是现场旋钮，无 TUNE 需求（契约 §38 电赛四问）。 */
#define LINK_EVENT_RETRY_TICKS  4u
#define LINK_EVENT_MAX_RETRY    8u

static bool     s_seeded;
static uint32_t s_period_base_ms;
static uint32_t s_hb_base_ms;

static uint32_t s_last_rx_count;
static uint32_t s_last_rx_ms;
static bool     s_ever_rx;
static bool     s_alive;
static uint32_t s_hb_sent;

/* 事件重试节拍与心跳抑制（策略状态归本服务；帧级计数归 codec）。 */
static uint8_t  s_ev_ticks;
static uint8_t  s_ev_retries;
static bool     s_data_txed;

/* LinkTest 遥测镜像（注册序 = 通道序 ch0..ch9）。 */
static bool s_telemetry_on;
static int  s_tx_alive;
static int  s_tx_rx_frames;
static int  s_tx_crc_errors;
static int  s_tx_ur_gap;
static int  s_tx_retx;
static int  s_tx_delivered;
static int  s_tx_ev_fail;
static int  s_tx_hb_sent;
static int  s_tx_overflows;
static int  s_tx_port_absent;

void Link_Init(void)
{
    Wireless_Init();
    s_seeded = false;
    s_period_base_ms = 0u;
    s_hb_base_ms = 0u;
    s_last_rx_count = 0u;
    s_last_rx_ms = 0u;
    s_ever_rx = false;
    s_alive = false;
    s_hb_sent = 0u;
    s_ev_ticks = 0u;
    s_ev_retries = 0u;
    s_data_txed = false;
}

void Link_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint32_t rx_count;

    if (!s_seeded) {
        s_seeded = true;
        s_period_base_ms = now_ms;
        s_hb_base_ms = now_ms;
        return;
    }
    elapsed_ms = now_ms - s_period_base_ms;
    if (elapsed_ms < LINK_UPDATE_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    Wireless_Poll();    /* RX 排空 + EVENT 即收即回 ACK + ACK 清 pending */

    /* 活性刷新：任意有效帧到达即刷新窗口。 */
    rx_count = Wireless_RxFrameCount();
    if (rx_count != s_last_rx_count) {
        s_last_rx_count = rx_count;
        s_last_rx_ms = now_ms;
        s_ever_rx = true;
    }
    s_alive = s_ever_rx && ((now_ms - s_last_rx_ms) <= LINK_ALIVE_WINDOW_MS);

    /* 事件重试节拍（梯队：先于状态/心跳）。 */
    if (Wireless_EventPending()) {
        s_ev_ticks++;
        if (s_ev_ticks >= LINK_EVENT_RETRY_TICKS) {
            s_ev_ticks = 0u;
            if (s_ev_retries >= LINK_EVENT_MAX_RETRY) {
                Wireless_AbandonEvent();    /* 必达失败明确化：ev_fail_count 留痕 */
            } else {
                s_ev_retries++;
                if (Wireless_ResendEvent()) {
                    s_data_txed = true;
                }
            }
        }
    }

    /* 200ms 心跳节拍（本服务唯一节拍所有者）；周期内已有成功数据 TX 则本拍抑制。 */
    if ((now_ms - s_hb_base_ms) >= LINK_HEARTBEAT_MS) {
        s_hb_base_ms = now_ms;
        if (s_data_txed) {
            s_data_txed = false;
        } else if (Wireless_SendHeartbeat()) {
            s_hb_sent++;
        }
    }

    if (s_telemetry_on) {
        Wireless_Diag_T diag;

        Wireless_GetDiag(&diag);
        s_tx_alive       = s_alive ? 1 : 0;
        s_tx_rx_frames   = (int)diag.frame_count;
        s_tx_crc_errors  = (int)diag.crc_error_count;
        s_tx_ur_gap      = (int)diag.ur_gap_count;
        s_tx_retx        = (int)diag.retx_count;
        s_tx_delivered   = (int)diag.delivered_count;
        s_tx_ev_fail     = (int)diag.ev_fail_count;
        s_tx_hb_sent     = (int)s_hb_sent;
        s_tx_overflows   = (int)diag.rx_overflows;
        s_tx_port_absent = diag.port_absent ? 1 : 0;
        vofa_run();
    }
}

bool Link_IsAlive(void)
{
    return s_alive;
}

bool Link_SendState(const uint8_t *data, uint8_t len)
{
    if (Wireless_SendState(data, len)) {
        s_data_txed = true;
        return true;
    }
    return false;
}

bool Link_TakeState(uint8_t *buf, uint8_t cap, uint8_t *len_out)
{
    return Wireless_TakeLatestState(buf, cap, len_out);
}

bool Link_SendEvent(const uint8_t *data, uint8_t len)
{
    if (Wireless_SendEvent(data, len)) {
        s_ev_ticks = 0u;
        s_ev_retries = 0u;
        s_data_txed = true;
        return true;
    }
    return false;
}

bool Link_EventBusy(void)
{
    return Wireless_EventPending();
}

bool Link_TakeEvent(uint8_t *buf, uint8_t cap, uint8_t *len_out)
{
    return Wireless_TakeEvent(buf, cap, len_out);
}

void Link_GetTelemetry(Link_Telemetry_T *out)
{
    Wireless_Diag_T diag;

    if (out == (void *)0) {
        return;
    }
    Wireless_GetDiag(&diag);
    out->alive        = s_alive;
    out->rx_frames    = diag.frame_count;
    out->crc_errors   = diag.crc_error_count;
    out->ur_gap       = diag.ur_gap_count;
    out->retx         = diag.retx_count;
    out->delivered    = diag.delivered_count;
    out->ev_fail      = diag.ev_fail_count;
    out->hb_sent      = s_hb_sent;
    out->rx_overflows = diag.rx_overflows;
    out->port_absent  = diag.port_absent;
}

void Link_StartTelemetry(void)
{
    vofa_clear_profile();
    s_tx_alive = 0;
    s_tx_rx_frames = 0;
    s_tx_crc_errors = 0;
    s_tx_ur_gap = 0;
    s_tx_retx = 0;
    s_tx_delivered = 0;
    s_tx_ev_fail = 0;
    s_tx_hb_sent = 0;
    s_tx_overflows = 0;
    s_tx_port_absent = 0;
    (void)vofa_register_int(&s_tx_alive);
    (void)vofa_register_int(&s_tx_rx_frames);
    (void)vofa_register_int(&s_tx_crc_errors);
    (void)vofa_register_int(&s_tx_ur_gap);
    (void)vofa_register_int(&s_tx_retx);
    (void)vofa_register_int(&s_tx_delivered);
    (void)vofa_register_int(&s_tx_ev_fail);
    (void)vofa_register_int(&s_tx_hb_sent);
    (void)vofa_register_int(&s_tx_overflows);
    (void)vofa_register_int(&s_tx_port_absent);
    s_telemetry_on = true;
}

void Link_StopTelemetry(void)
{
    s_telemetry_on = false;
    vofa_clear_profile();
}
