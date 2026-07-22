/**
 * @file    link.c
 * @brief   无线链路服务实现：心跳节拍/活性窗口唯一所有者。
 */
#include "app/service/link/link.h"

#include "driver/uart_wireless/uart_wireless.h"
#include "driver/uart_vofa/uart_vofa.h"

#define LINK_UPDATE_PERIOD_MS  10u
#define LINK_HEARTBEAT_MS      200u
#define LINK_ALIVE_WINDOW_MS   600u

static bool     s_seeded;
static uint32_t s_period_base_ms;
static uint32_t s_hb_base_ms;

static uint32_t s_last_rx_count;
static uint32_t s_last_rx_ms;
static bool     s_ever_rx;
static bool     s_alive;
static uint32_t s_hb_sent;

/* LinkTest 遥测镜像（注册序 = 通道序 ch0..ch5）。 */
static bool s_telemetry_on;
static int  s_tx_alive;
static int  s_tx_rx_frames;
static int  s_tx_crc_errors;
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

    Wireless_Poll();

    /* 活性刷新：任意有效帧到达即刷新窗口。 */
    rx_count = Wireless_RxFrameCount();
    if (rx_count != s_last_rx_count) {
        s_last_rx_count = rx_count;
        s_last_rx_ms = now_ms;
        s_ever_rx = true;
    }
    s_alive = s_ever_rx && ((now_ms - s_last_rx_ms) <= LINK_ALIVE_WINDOW_MS);

    /* 200ms 心跳节拍（本服务唯一节拍所有者）。 */
    if ((now_ms - s_hb_base_ms) >= LINK_HEARTBEAT_MS) {
        s_hb_base_ms = now_ms;
        if (Wireless_SendHeartbeat()) {
            s_hb_sent++;
        }
    }

    if (s_telemetry_on) {
        Wireless_Diag_T diag;

        Wireless_GetDiag(&diag);
        s_tx_alive       = s_alive ? 1 : 0;
        s_tx_rx_frames   = (int)diag.frame_count;
        s_tx_crc_errors  = (int)diag.crc_error_count;
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

bool Link_Send(const uint8_t *data, uint8_t len)
{
    return Wireless_SendUser(data, len);
}

bool Link_TakeLatest(uint8_t *buf, uint8_t cap, uint8_t *len_out)
{
    return Wireless_TakeLatestUser(buf, cap, len_out);
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
    s_tx_hb_sent = 0;
    s_tx_overflows = 0;
    s_tx_port_absent = 0;
    (void)vofa_register_int(&s_tx_alive);
    (void)vofa_register_int(&s_tx_rx_frames);
    (void)vofa_register_int(&s_tx_crc_errors);
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
