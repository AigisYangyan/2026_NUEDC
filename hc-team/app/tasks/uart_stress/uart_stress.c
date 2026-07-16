/**
 * @file    uart_stress.c
 * @brief   UART_Stress 压测模块实现
 *
 * 本文件实现 230400 波特率、5ms 周期双向收发的 UART 压力测试。
 *
 * 工作流程：
 * 1. Enter 时接管 stepmotor UART 的 RX 回调，暂停 StepmotorBus
 * 2. 每 5ms 主动发一帧 8 字节“模拟电机控制指令”
 * 3. RX 采用 AA55 + checksum 滑窗解帧，提取有效 8B 帧后再 echo
 * 4. 心跳/字节/错误计数同步更新，并驱动 GPIO_LED 闪烁
 * 5. Exit 时恢复 StepmotorBus 的 RX 回调并清计数
 *
 * 设计约定：
 * - TX 帧：{0xAA,0x55, seq, 0x11,0x22,0x33,0x44, csum(前7字节求和低8位)}
 * - RX 不再按“每 8 字节硬切”；改为 AA55 + checksum 滑窗解帧
 * - 所有耗时操作均为非阻塞；发送忙拒则计入 err_count
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app/tasks/uart_stress/uart_stress.h"

#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "ti_msp_dl_config.h"
#include "app/tasks/platform_2d/stepmotor_bus.h"

/* ---- 静态配置 ----------------------------------------------------------- */

#define UART_STRESS_FRAME_LEN          8u
#define UART_STRESS_RX_FIFO_SIZE       STEPMOTOR_BUS_SHARED_RX_FIFO_SIZE
#define UART_STRESS_LED_BLINK_INTERVAL 20u

/* ---- 模块状态 ----------------------------------------------------------- */

static volatile bool s_entered = false;
static uint32_t s_heartbeat = 0u;
static uint32_t s_tx_bytes = 0u;
static uint32_t s_rx_bytes = 0u;
static uint32_t s_tx_frames = 0u;
static uint32_t s_rx_frames = 0u;
static uint32_t s_err_count = 0u;
static uint8_t  s_tx_seq = 0u;
static uint8_t  s_led_blink_div = 0u;

/* RX 环形 FIFO（ISR 写，Tick 读） */
static volatile uint8_t s_rx_buf[UART_STRESS_RX_FIFO_SIZE];
static volatile uint16_t s_rx_head = 0u;
static volatile uint16_t s_rx_tail = 0u;
static volatile uint16_t s_rx_count = 0u;

/* ---- 静态辅助函数 ------------------------------------------------------- */

/* 在 UART RX ISR 上下文运行：单字节入 FIFO，满时丢最旧 */
static void uart_stress_rx_isr(uint8_t data)
{
    if (s_rx_count >= UART_STRESS_RX_FIFO_SIZE) {
        /* FIFO 满，丢最旧字节并计为错误 */
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART_STRESS_RX_FIFO_SIZE);
        s_rx_count--;
        s_err_count++;
    }

    s_rx_buf[s_rx_head] = data;
    s_rx_head = (uint16_t)((s_rx_head + 1u) % UART_STRESS_RX_FIFO_SIZE);
    s_rx_count++;
    s_rx_bytes++;
}

static bool uart_stress_rx_peek(uint16_t offset, uint8_t* out)
{
    uint16_t index = 0u;

    if ((out == NULL) || (offset >= s_rx_count)) {
        return false;
    }

    index = (uint16_t)((s_rx_tail + offset) % UART_STRESS_RX_FIFO_SIZE);
    *out = s_rx_buf[index];
    return true;
}

static void uart_stress_rx_drop(uint16_t n)
{
    if (n >= s_rx_count) {
        s_rx_head = 0u;
        s_rx_tail = 0u;
        s_rx_count = 0u;
        return;
    }

    s_rx_tail = (uint16_t)((s_rx_tail + n) % UART_STRESS_RX_FIFO_SIZE);
    s_rx_count = (uint16_t)(s_rx_count - n);
}

static bool uart_stress_rx_copy(uint8_t* out, uint16_t n)
{
    uint16_t i = 0u;

    if ((out == NULL) || (n > s_rx_count)) {
        return false;
    }

    for (i = 0u; i < n; i++) {
        if (uart_stress_rx_peek(i, &out[i]) == false) {
            return false;
        }
    }

    return true;
}

static uint8_t uart_stress_checksum(const uint8_t* frame)
{
    uint8_t i = 0u;
    uint8_t sum = 0u;

    if (frame == NULL) {
        return 0u;
    }

    for (i = 0u; i < (UART_STRESS_FRAME_LEN - 1u); i++) {
        sum = (uint8_t)(sum + frame[i]);
    }

    return sum;
}

/* 滑窗查找 AA55 + checksum 的完整测试帧。 */
static bool uart_stress_try_extract_frame(uint8_t* frame)
{
    uint8_t header0 = 0u;
    uint8_t header1 = 0u;

    if (frame == NULL) {
        return false;
    }

    while (s_rx_count >= 2u) {
        if (uart_stress_rx_peek(0u, &header0) == false) {
            return false;
        }

        if (header0 != 0xAAu) {
            uart_stress_rx_drop(1u);
            s_err_count++;
            continue;
        }

        if (uart_stress_rx_peek(1u, &header1) == false) {
            return false;
        }

        if (header1 != 0x55u) {
            uart_stress_rx_drop(1u);
            s_err_count++;
            continue;
        }

        if (s_rx_count < UART_STRESS_FRAME_LEN) {
            return false;
        }

        if (uart_stress_rx_copy(frame, UART_STRESS_FRAME_LEN) == false) {
            return false;
        }

        if (uart_stress_checksum(frame) == frame[UART_STRESS_FRAME_LEN - 1u]) {
            uart_stress_rx_drop(UART_STRESS_FRAME_LEN);
            return true;
        }

        /* 头对上但校验失败，滑窗前进 1 字节继续找下一帧。 */
        uart_stress_rx_drop(1u);
        s_err_count++;
    }

    return false;
}

/* 构造一帧 8B 测试帧，校验和=前 7 字节之和低 8 位 */
static void uart_stress_build_tx_frame(uint8_t* frame)
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

/* UART 压测运行在协作式主循环里，不能在任务上下文自旋等 DMA。 */
static bool uart_stress_tx_idle(void)
{
    return (Mspm0Runtime_IsStepmotorTxBusy() == false) ? true : false;
}

static int uart_stress_send_frame_if_idle(const uint8_t* frame)
{
    if ((frame == NULL) || (uart_stress_tx_idle() == false)) {
        return -11;
    }

    if (Mspm0Runtime_SendStepmotor(frame, (uint32_t)UART_STRESS_FRAME_LEN) ==
        false) {
        return -11;
    }

    return 0;
}

static void uart_stress_led_init(void)
{
    /* GPIO 方向/上下拉由 SysConfig 生成代码统一初始化，这里只控制电平。 */
    DL_GPIO_clearPins(GPIO_STATUS_LED_PORT, GPIO_STATUS_LED_PIN_22_PIN);
    DL_GPIO_enableOutput(GPIO_STATUS_LED_PORT, GPIO_STATUS_LED_PIN_22_PIN);
}

static void uart_stress_led_off(void)
{
    DL_GPIO_clearPins(GPIO_STATUS_LED_PORT, GPIO_STATUS_LED_PIN_22_PIN);
}

static void uart_stress_led_toggle(void)
{
    DL_GPIO_togglePins(GPIO_STATUS_LED_PORT, GPIO_STATUS_LED_PIN_22_PIN);
}

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 初始化压测模块本地状态
 * @note  仅清零计数；Enter 时才真正接管 RX
 */
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
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_rx_count = 0u;
}

/**
 * @brief 进入运行项，接管 UART RX 并暂停 StepmotorBus
 */
void UartStress_Enter(void)
{
    UartStress_Init();

    /* 暂停 StepmotorBus 消费与 TX 调度 */
    StepmotorBus_SetBypass(true);

    /* 接管 UART RX 回调，指向本模块 ISR */
    Mspm0Runtime_SetStepmotorRxCallback(uart_stress_rx_isr);

    uart_stress_led_init();
    s_entered = true;
}

/**
 * @brief 退出运行项，恢复 StepmotorBus RX 回调
 */
void UartStress_Exit(void)
{
    s_entered = false;

    /* 恢复 StepmotorBus RX 回调 */
    Mspm0Runtime_SetStepmotorRxCallback(StepmotorBus_RxISR);

    /* 解除 StepmotorBus 旁路 */
    StepmotorBus_SetBypass(false);
    uart_stress_led_off();
}

/**
 * @brief 5ms 周期入口：主动发 + 滑窗解帧 + echo + 心跳
 */
void UartStress_Tick5ms(void)
{
    uint8_t tx_frame[UART_STRESS_FRAME_LEN];
    uint8_t echo_frame[UART_STRESS_FRAME_LEN];
    int err;
    bool tx_sent = false;

    if (s_entered == false) {
        return;
    }

    s_heartbeat++;

    /*
     * --- 1. 先尝试滑窗提取一帧再回显 ---
     * 第二次压测后，结论是当前瓶颈更偏向高波特率下的接收/分帧策略，
     * 而不是 FIFO 容量本身；因此这里改为 AA55 + checksum 滑窗解帧。
     */
    if ((s_rx_count >= UART_STRESS_FRAME_LEN) &&
        (uart_stress_tx_idle() == true)) {
        if (uart_stress_try_extract_frame(echo_frame) == true) {
            err = uart_stress_send_frame_if_idle(echo_frame);
            if (err == 0) {
                s_tx_bytes += UART_STRESS_FRAME_LEN;
                s_tx_frames++;
                s_rx_frames++;
                tx_sent = true;
            }
            else {
                s_err_count++;
            }
        }
    }

    /* --- 2. TX 空闲且本 tick 未做 echo 时，主动发一帧测试帧 --- */
    if (tx_sent == false) {
        uart_stress_build_tx_frame(tx_frame);
        err = uart_stress_send_frame_if_idle(tx_frame);
        if (err == 0) {
            s_tx_bytes += UART_STRESS_FRAME_LEN;
            s_tx_frames++;
        }
        else {
            s_err_count++;
        }
    }

    /* --- 3. 每 100ms 翻转一次 GPIO_LED --- */
    s_led_blink_div++;
    if (s_led_blink_div >= UART_STRESS_LED_BLINK_INTERVAL) {
        s_led_blink_div = 0u;
        uart_stress_led_toggle();
    }
}
