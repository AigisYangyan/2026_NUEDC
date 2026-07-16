/**
 * @file    uart_stress.c
 * @brief   UART_Stress 压测模块实现
 *
 * 本文件实现 230400 波特率、5ms 周期双向收发的 UART 压力测试。
 * 进入运行项时通过 StepmotorBus 暂停普通消费者，然后在任务态经
 * StepmotorUart 读写测试帧，不再替换 runtime 回调。
 */

#include "app/tasks/uart_stress/uart_stress.h"

#include "app/tasks/platform_2d/stepmotor_bus.h"
#include "driver/board_uart/stepmotor_uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UART_STRESS_FRAME_LEN          8u
#define UART_STRESS_LED_BLINK_INTERVAL 20u
#define UART_STRESS_RX_WINDOW_SIZE     UART_STRESS_FRAME_LEN
#define UART_STRESS_READ_CHUNK_SIZE    32u

static bool s_entered = false;
static uint32_t s_heartbeat = 0u;
static uint32_t s_tx_bytes = 0u;
static uint32_t s_rx_bytes = 0u;
static uint32_t s_tx_frames = 0u;
static uint32_t s_rx_frames = 0u;
static uint32_t s_err_count = 0u;
static uint8_t s_tx_seq = 0u;
static uint8_t s_led_blink_div = 0u;
static uint8_t s_rx_window[UART_STRESS_RX_WINDOW_SIZE];
static uint8_t s_rx_window_count = 0u;

static uint8_t uart_stress_checksum(const uint8_t *frame)
{
    uint8_t index = 0u;
    uint8_t sum = 0u;

    if (frame == NULL) {
        return 0u;
    }

    for (index = 0u; index < (UART_STRESS_FRAME_LEN - 1u); index++) {
        sum = (uint8_t)(sum + frame[index]);
    }

    return sum;
}

static void uart_stress_drop_prefix(uint8_t drop_count)
{
    if (drop_count >= s_rx_window_count) {
        s_rx_window_count = 0u;
        return;
    }

    memmove(s_rx_window,
            &s_rx_window[drop_count],
            (size_t)(s_rx_window_count - drop_count));
    s_rx_window_count = (uint8_t)(s_rx_window_count - drop_count);
}

static bool uart_stress_try_extract_frame(uint8_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    while (s_rx_window_count > 0u) {
        if (s_rx_window[0] != 0xAAu) {
            uart_stress_drop_prefix(1u);
            s_err_count++;
            continue;
        }

        if (s_rx_window_count < 2u) {
            return false;
        }
        if (s_rx_window[1] != 0x55u) {
            uart_stress_drop_prefix(1u);
            s_err_count++;
            continue;
        }

        if (s_rx_window_count < UART_STRESS_FRAME_LEN) {
            return false;
        }

        if (uart_stress_checksum(s_rx_window) == s_rx_window[UART_STRESS_FRAME_LEN - 1u]) {
            memcpy(frame, s_rx_window, UART_STRESS_FRAME_LEN);
            uart_stress_drop_prefix(UART_STRESS_FRAME_LEN);
            return true;
        }

        uart_stress_drop_prefix(1u);
        s_err_count++;
    }

    return false;
}

static void uart_stress_process_rx_byte(uint8_t data)
{
    if (s_rx_window_count >= UART_STRESS_RX_WINDOW_SIZE) {
        uart_stress_drop_prefix(1u);
        s_err_count++;
    }

    s_rx_window[s_rx_window_count++] = data;
}

static void uart_stress_build_tx_frame(uint8_t *frame)
{
    frame[0] = 0xAAu;
    frame[1] = 0x55u;
    frame[2] = s_tx_seq;
    frame[3] = 0x11u;
    frame[4] = 0x22u;
    frame[5] = 0x33u;
    frame[6] = 0x44u;
    frame[7] = uart_stress_checksum(frame);
    s_tx_seq++;
}

static void uart_stress_drain_rx(void)
{
    uint8_t read_buf[UART_STRESS_READ_CHUNK_SIZE];
    uint32_t read_count = 0u;
    uint32_t index = 0u;

    do {
        read_count = StepmotorUart_Read(read_buf, sizeof(read_buf));
        s_rx_bytes += read_count;
        for (index = 0u; index < read_count; index++) {
            uart_stress_process_rx_byte(read_buf[index]);
        }
    } while (read_count > 0u);
}

static bool uart_stress_send_frame_if_idle(const uint8_t *frame)
{
    if ((frame == NULL) || (StepmotorUart_IsTxIdle() == false)) {
        return false;
    }

    return StepmotorUart_TryWrite(frame, UART_STRESS_FRAME_LEN);
}

void UartStress_Init(void)
{
    s_entered = false;
    s_heartbeat = 0u;
    s_tx_bytes = 0u;
    s_rx_bytes = 0u;
    s_tx_frames = 0u;
    s_rx_frames = 0u;
    s_err_count = 0u;
    s_tx_seq = 0u;
    s_led_blink_div = 0u;
    s_rx_window_count = 0u;
    memset(s_rx_window, 0, sizeof(s_rx_window));
}

void UartStress_Enter(void)
{
    UartStress_Init();

    if (StepmotorBus_RequestBypass() == false) {
        s_err_count++;
        return;
    }

    s_entered = true;
}

void UartStress_Exit(void)
{
    s_entered = false;
    StepmotorBus_SetBypass(false);
}

void UartStress_Tick5ms(void)
{
    uint8_t tx_frame[UART_STRESS_FRAME_LEN];
    uint8_t echo_frame[UART_STRESS_FRAME_LEN];
    bool tx_sent = false;

    if (s_entered == false) {
        return;
    }

    s_heartbeat++;
    uart_stress_drain_rx();

    if (StepmotorUart_ConsumeTxDone() == true) {
    }

    if (uart_stress_try_extract_frame(echo_frame) == true) {
        if (uart_stress_send_frame_if_idle(echo_frame) == true) {
            s_tx_bytes += UART_STRESS_FRAME_LEN;
            s_tx_frames++;
            s_rx_frames++;
            tx_sent = true;
        } else {
            s_err_count++;
        }
    }

    if (tx_sent == false) {
        uart_stress_build_tx_frame(tx_frame);
        if (uart_stress_send_frame_if_idle(tx_frame) == true) {
            s_tx_bytes += UART_STRESS_FRAME_LEN;
            s_tx_frames++;
        } else {
            s_err_count++;
        }
    }

    s_led_blink_div++;
    if (s_led_blink_div >= UART_STRESS_LED_BLINK_INTERVAL) {
        s_led_blink_div = 0u;
    }
}
