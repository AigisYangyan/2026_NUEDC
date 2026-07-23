/**
 * @file    uart_check.c
 * @brief   串口链路诊断服务实现：六计数只读镜像 + VOFA tx×6。
 */
#include "app/service/uart_check/uart_check.h"

#include <stdbool.h>

#include "driver/board_uart/vofa_uart.h"
#include "driver/board_uart/vision_uart.h"
#include "driver/board_uart/stepmotor_uart.h"
#include "driver/board_uart/imu_uart.h"
#include "driver/board_uart/wireless_uart.h"
#include "driver/uart_vofa/uart_vofa.h"

#define UART_CHECK_PERIOD_MS 10u

static bool     s_active;
static bool     s_seeded;
static uint32_t s_base_ms;

static UartCheck_Telemetry_T s_snap;

/* VOFA 通道镜像（注册序 = 通道序 ch0..ch5）。 */
static int s_tx_vofa_rx;
static int s_tx_vofa_tx;
static int s_tx_vision_rx;
static int s_tx_step_rx;
static int s_tx_imu_rx;
static int s_tx_wl_rx;

void UartCheck_Start(void)
{
    vofa_clear_profile();
    s_tx_vofa_rx = 0;
    s_tx_vofa_tx = 0;
    s_tx_vision_rx = 0;
    s_tx_step_rx = 0;
    s_tx_imu_rx = 0;
    s_tx_wl_rx = 0;
    (void)vofa_register_int(&s_tx_vofa_rx);
    (void)vofa_register_int(&s_tx_vofa_tx);
    (void)vofa_register_int(&s_tx_vision_rx);
    (void)vofa_register_int(&s_tx_step_rx);
    (void)vofa_register_int(&s_tx_imu_rx);
    (void)vofa_register_int(&s_tx_wl_rx);
    s_snap.vofa_rx_ovf = 0u;
    s_snap.vofa_tx_ovf = 0u;
    s_snap.vision_rx_ovf = 0u;
    s_snap.step_rx_ovf = 0u;
    s_snap.imu_rx_ovf = 0u;
    s_snap.wl_rx_ovf = 0u;
    s_seeded = false;
    s_active = true;
}

void UartCheck_Update(uint32_t now_ms)
{
    if (!s_active) {
        return;
    }
    if (!s_seeded) {
        s_seeded = true;
        s_base_ms = now_ms;
        return;
    }
    if ((now_ms - s_base_ms) < UART_CHECK_PERIOD_MS) {
        return;
    }
    s_base_ms = now_ms;

    s_snap.vofa_rx_ovf   = VofaUart_GetRxOverflowCount();
    s_snap.vofa_tx_ovf   = VofaUart_GetTxOverflowCount();
    s_snap.vision_rx_ovf = VisionUart_GetRxOverflowCount();
    s_snap.step_rx_ovf   = StepmotorUart_GetRxOverflowCount();
    s_snap.imu_rx_ovf    = ImuUart_GetRxOverflowCount();
    s_snap.wl_rx_ovf     = WirelessUart_GetRxOverflowCount();

    s_tx_vofa_rx   = (int)s_snap.vofa_rx_ovf;
    s_tx_vofa_tx   = (int)s_snap.vofa_tx_ovf;
    s_tx_vision_rx = (int)s_snap.vision_rx_ovf;
    s_tx_step_rx   = (int)s_snap.step_rx_ovf;
    s_tx_imu_rx    = (int)s_snap.imu_rx_ovf;
    s_tx_wl_rx     = (int)s_snap.wl_rx_ovf;
    vofa_run();
}

void UartCheck_Stop(void)
{
    s_active = false;
    vofa_clear_profile();
}

void UartCheck_GetTelemetry(UartCheck_Telemetry_T *out)
{
    if (out == (void *)0) {
        return;
    }
    *out = s_snap;
}
