/**
 * @file    fake_wireless_port.c
 * @brief   wireless_uart 端口的主机侧 fake：RX 注入 + TX 抓取 + 在场/缺席开关。
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FWP_RX_CAP 512u
#define FWP_TX_CAP 512u

static bool     s_present = true;
static uint8_t  s_rx[FWP_RX_CAP];
static uint32_t s_rx_head;
static uint32_t s_rx_len;
static uint8_t  s_tx[FWP_TX_CAP];
static uint32_t s_tx_len;

void FakeWirelessPort_Reset(void)
{
    s_present = true;
    s_rx_head = 0u;
    s_rx_len = 0u;
    s_tx_len = 0u;
}

void FakeWirelessPort_SetPresent(bool present)
{
    s_present = present;
}

void FakeWirelessPort_PushRx(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0u; (i < len) && (s_rx_len < FWP_RX_CAP); i++) {
        s_rx[(s_rx_head + s_rx_len) % FWP_RX_CAP] = data[i];
        s_rx_len++;
    }
}

uint32_t FakeWirelessPort_CopyTx(uint8_t *out, uint32_t cap)
{
    uint32_t n = (s_tx_len < cap) ? s_tx_len : cap;

    memcpy(out, s_tx, n);
    return n;
}

void FakeWirelessPort_ClearTx(void)
{
    s_tx_len = 0u;
}

/* ---- wireless_uart API 的 fake 实现 -------------------------------------- */

bool WirelessUart_Init(void)
{
    return s_present;
}

uint32_t WirelessUart_Read(uint8_t *buf, uint32_t cap)
{
    uint32_t n = 0u;

    while ((n < cap) && (s_rx_len > 0u)) {
        buf[n++] = s_rx[s_rx_head];
        s_rx_head = (s_rx_head + 1u) % FWP_RX_CAP;
        s_rx_len--;
    }
    return n;
}

bool WirelessUart_Write(const uint8_t *data, uint32_t len)
{
    if (!s_present || ((s_tx_len + len) > FWP_TX_CAP)) {
        return false;
    }
    memcpy(&s_tx[s_tx_len], data, len);
    s_tx_len += len;
    return true;
}

uint32_t WirelessUart_GetRxOverflowCount(void)
{
    return 0u;      /* fake FIFO 512B 远大于用例注入量，不模拟溢出 */
}
