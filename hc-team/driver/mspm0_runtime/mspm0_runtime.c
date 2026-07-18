/**
 * @file    mspm0_runtime.c
 * @brief   Board-specific MSPM0G3519 runtime: shared GPIO IRQ, UART/DMA IRQ
 *          dispatch, and hardware QEI readout.
 *
 * Owns GROUP1 (keys), DMA, and the fixed board UART IRQ symbols.
 * Encoder counting moved from GROUP1 software edge-counting (G3507) to the
 * two hardware QEI timers (G3519: QEI_LEFT=TIMG8, QEI_RIGHT=TIMG9 since the
 * 2026-07-17 netlist-driven $name swap; single source of truth: board.syscfg;
 * code only uses QEI_*_INST macros).
 * SysTick is now owned by driver/clock.
 */
#include "driver/mspm0_runtime/mspm0_runtime.h"

#include "driver/bsl_entry/bsl_entry.h"
#include "driver/clock/clock.h"
#include "ti_msp_dl_config.h"

#include <string.h>
#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_gpio.h>
#include <ti/driverlib/dl_timerg.h>
#include <ti/driverlib/dl_uart.h>

/* SysConfig DMA channel IDs for the three fixed DMA RX/TX roles. */
#define RT_DMA_STEPMOTOR_RX DMA_CH0_CHAN_ID
#define RT_DMA_STEPMOTOR_TX DMA_CH3_CHAN_ID
#define RT_DMA_VOFA_TX      DMA_CH1_CHAN_ID
#define RT_DMA_VOFA_RX      DMA_CH2_CHAN_ID
#define RT_DMA_VISION_TX    DMA_CH8_CHAN_ID
#define RT_DMA_VISION_RX    DMA_CH7_CHAN_ID

/* Hardware QEI readout state: the 16-bit QEI counter (LOAD = 65535) is
 * widened to int32_t by accumulating signed 16-bit deltas between reads.
 * Precondition: Mspm0Runtime_GetEncoderCounts() is called at least once per
 * 32767 counts of wheel travel (~21 wheel revs at 1560 counts/rev), which
 * the periodic encoder task satisfies by orders of magnitude. */
static uint16_t s_qei_last_left = 0u;
static uint16_t s_qei_last_right = 0u;
static int32_t s_left_encoder_count = 0;
static int32_t s_right_encoder_count = 0;
static volatile uint8_t s_key_irq_edges = 0u;

static uint8_t s_dma_rx_byte_stepmotor;
static uint8_t s_dma_rx_byte_vofa;
static uint8_t s_dma_rx_byte_vision;

void StepmotorUart_IsrPushByte(uint8_t data);
void StepmotorUart_IsrTxDone(void);
void VofaUart_IsrPushByte(uint8_t data);
void VofaUart_IsrTxDone(void);
void VisionUart_IsrPushByte(uint8_t data);
void VisionUart_IsrTxDone(void);
void ImuUart_IsrPushByte(uint8_t data);
/* BslEntry_IsrOnByte 声明取自 driver/bsl_entry/bsl_entry.h（其头暴露该 ISR 契约符号，
 * 故 include 获原型校验；board_uart 各 *_IsrPushByte 未在头暴露才用上面的本地前向声明）。 */

static uint32_t runtime_dma_irq_mask(uint8_t dma_ch_num)
{
    switch (dma_ch_num) {
    case 0u:
        return DL_DMA_INTERRUPT_CHANNEL0;
    case 1u:
        return DL_DMA_INTERRUPT_CHANNEL1;
    case 2u:
        return DL_DMA_INTERRUPT_CHANNEL2;
    case 3u:
        return DL_DMA_INTERRUPT_CHANNEL3;
    case 4u:
        return DL_DMA_INTERRUPT_CHANNEL4;
    case 5u:
        return DL_DMA_INTERRUPT_CHANNEL5;
    case 6u:
        return DL_DMA_INTERRUPT_CHANNEL6;
    default:
        return 0u;
    }
}

static void runtime_drain_uart_rx(UART_Regs *uart, void (*push_byte)(uint8_t))
{
    uint8_t data = 0u;

    if ((uart == NULL) || (push_byte == NULL)) {
        return;
    }

    while (DL_UART_receiveDataCheck(uart, &data)) {
        push_byte(data);
    }
}

static void runtime_start_dma_rx(uint8_t dma_ch,
                                 UART_Regs *uart,
                                 uint8_t *byte_buf)
{
    DL_UART_clearInterruptStatus(uart, DL_UART_INTERRUPT_DMA_DONE_RX);
    DL_DMA_disableChannel(DMA, dma_ch);
    DL_DMA_setSrcAddr(DMA, dma_ch, (uint32_t)(uintptr_t)&uart->RXDATA);
    DL_DMA_setDestAddr(DMA, dma_ch, (uint32_t)(uintptr_t)byte_buf);
    DL_DMA_setTransferSize(DMA, dma_ch, 1u);
    DL_DMA_enableChannel(DMA, dma_ch);
}

/**
 * @brief Map a uint16_t to int16_t modulo 2^16 via bitwise reinterpretation.
 *
 * Same memcpy type-punning rationale as encoder.c's u32_mod_i32(): the only
 * conversion free of implementation-defined behavior in standard C.
 */
static int16_t runtime_u16_mod_i16(uint16_t u)
{
    int16_t s;
    memcpy(&s, &u, sizeof(s));
    return s;
}

/**
 * @brief Fold the current 16-bit QEI counter into a 32-bit accumulator.
 * @param counter_now Current hardware QEI counter value.
 * @param last        Last sampled counter value; updated in place.
 * @param accum       32-bit accumulated count; updated in place.
 *
 * Unsigned subtraction wraps modulo 2^16, so a signed reinterpretation of
 * the difference yields the true delta as long as fewer than 32768 counts
 * elapsed between samples (see precondition at the state definitions).
 */
static void runtime_qei_accumulate(uint16_t counter_now,
                                   uint16_t *last,
                                   int32_t *accum)
{
    uint16_t diff_u = (uint16_t)(counter_now - *last);

    *last = counter_now;
    *accum += (int32_t)runtime_u16_mod_i16(diff_u);
}

static void runtime_handle_key_irqs(uint32_t pending_a, uint32_t pending_b)
{
    uint8_t key_edges = 0u;

    if ((GPIO_GRP_KEY_K1_PORT == GPIOB) &&
        ((pending_b & GPIO_GRP_KEY_K1_PIN) != 0u)) {
        key_edges |= (1u << 0);
    } else if ((GPIO_GRP_KEY_K1_PORT == GPIOA) &&
               ((pending_a & GPIO_GRP_KEY_K1_PIN) != 0u)) {
        key_edges |= (1u << 0);
    }

    if ((GPIO_GRP_KEY_K2_PORT == GPIOB) &&
        ((pending_b & GPIO_GRP_KEY_K2_PIN) != 0u)) {
        key_edges |= (1u << 1);
    } else if ((GPIO_GRP_KEY_K2_PORT == GPIOA) &&
               ((pending_a & GPIO_GRP_KEY_K2_PIN) != 0u)) {
        key_edges |= (1u << 1);
    }

    if ((GPIO_GRP_KEY_K3_PORT == GPIOB) &&
        ((pending_b & GPIO_GRP_KEY_K3_PIN) != 0u)) {
        key_edges |= (1u << 2);
    } else if ((GPIO_GRP_KEY_K3_PORT == GPIOA) &&
               ((pending_a & GPIO_GRP_KEY_K3_PIN) != 0u)) {
        key_edges |= (1u << 2);
    }

    if ((GPIO_GRP_KEY_K4_PORT == GPIOA) &&
        ((pending_a & GPIO_GRP_KEY_K4_PIN) != 0u)) {
        key_edges |= (1u << 3);
    } else if ((GPIO_GRP_KEY_K4_PORT == GPIOB) &&
               ((pending_b & GPIO_GRP_KEY_K4_PIN) != 0u)) {
        key_edges |= (1u << 3);
    }

    s_key_irq_edges |= key_edges;
}

static void runtime_handle_uart_irq(UART_Regs *uart, void (*push_byte)(uint8_t))
{
    uint32_t irq_status = 0u;

    irq_status = DL_UART_getEnabledInterruptStatus(
        uart,
        DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_DMA_DONE_RX |
            DL_UART_INTERRUPT_DMA_DONE_TX);

    if ((irq_status & DL_UART_INTERRUPT_RX) != 0u) {
        runtime_drain_uart_rx(uart, push_byte);
        DL_UART_clearInterruptStatus(uart, DL_UART_INTERRUPT_RX);
    }

    if ((irq_status & DL_UART_INTERRUPT_DMA_DONE_RX) != 0u) {
        DL_UART_clearInterruptStatus(uart, DL_UART_INTERRUPT_DMA_DONE_RX);
    }

    if ((irq_status & DL_UART_INTERRUPT_DMA_DONE_TX) != 0u) {
        DL_UART_clearInterruptStatus(uart, DL_UART_INTERRUPT_DMA_DONE_TX);
    }
}

void Mspm0Runtime_InitUartDma(void)
{
    const uint8_t dma_channels[] = {
        RT_DMA_STEPMOTOR_RX,
        RT_DMA_STEPMOTOR_TX,
        RT_DMA_VOFA_RX,
        RT_DMA_VOFA_TX,
        RT_DMA_VISION_RX,
        RT_DMA_VISION_TX,
    };
    uint32_t index = 0u;

    for (index = 0u; index < (sizeof(dma_channels) / sizeof(dma_channels[0])); index++) {
        uint32_t irq_mask = runtime_dma_irq_mask(dma_channels[index]);
        if (irq_mask != 0u) {
            DL_DMA_clearInterruptStatus(DMA, irq_mask);
            DL_DMA_enableInterrupt(DMA, irq_mask);
        }
    }

    /* Single-byte DMA RX needs RX FIFO trigger at one entry. */
    DL_UART_Main_setRXFIFOThreshold(UART_STEPPER_BUS_INST,
                                    DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setRXFIFOThreshold(UART_HOST_LINK_INST,
                                    DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setRXFIFOThreshold(UART_VISION_INST,
                                    DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);

    runtime_start_dma_rx(RT_DMA_STEPMOTOR_RX,
                         UART_STEPPER_BUS_INST,
                         &s_dma_rx_byte_stepmotor);
    runtime_start_dma_rx(RT_DMA_VOFA_RX, UART_HOST_LINK_INST, &s_dma_rx_byte_vofa);
    runtime_start_dma_rx(RT_DMA_VISION_RX,
                         UART_VISION_INST,
                         &s_dma_rx_byte_vision);
}

void Mspm0Runtime_DelayMs(uint32_t delay_ms)
{
    uint32_t start_ms = 0u;
    uint32_t now_ms = 0u;

    if (delay_ms == 0u) {
        return;
    }

    start_ms = Clock_NowMs();
    do {
        now_ms = Clock_NowMs();
    } while ((now_ms - start_ms) < delay_ms);
}

void Mspm0Runtime_GetEncoderCounts(int32_t *left, int32_t *right)
{
    int32_t local_left = 0;
    int32_t local_right = 0;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    runtime_qei_accumulate((uint16_t)DL_TimerG_getTimerCount(QEI_LEFT_INST),
                           &s_qei_last_left,
                           &s_left_encoder_count);
    runtime_qei_accumulate((uint16_t)DL_TimerG_getTimerCount(QEI_RIGHT_INST),
                           &s_qei_last_right,
                           &s_right_encoder_count);
    local_left = s_left_encoder_count;
    local_right = s_right_encoder_count;
    __set_PRIMASK(primask);

    if (left != NULL) {
        *left = local_left;
    }
    if (right != NULL) {
        *right = local_right;
    }
}

uint8_t Mspm0Runtime_ConsumeKeyIrqEdges(void)
{
    uint8_t key_edges = 0u;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    key_edges = s_key_irq_edges;
    s_key_irq_edges = 0u;
    __set_PRIMASK(primask);

    return key_edges;
}

void GROUP1_IRQHandler(void)
{
    uint32_t pending_a = 0u;
    uint32_t pending_b = 0u;

    pending_a = DL_GPIO_getEnabledInterruptStatus(GPIOA, 0xFFFFFFFFu);
    pending_b = DL_GPIO_getEnabledInterruptStatus(GPIOB, 0xFFFFFFFFu);

    if ((pending_a | pending_b) == 0u) {
        return;
    }

    DL_GPIO_clearInterruptStatus(GPIOA, pending_a);
    DL_GPIO_clearInterruptStatus(GPIOB, pending_b);

    runtime_handle_key_irqs(pending_a, pending_b);
}

void DMA_IRQHandler(void)
{
    uint32_t pending_irq = DL_DMA_getEnabledInterruptStatus(DMA, 0xFFFFFFFFu);

    if (pending_irq == 0u) {
        return;
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_STEPMOTOR_RX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_STEPMOTOR_RX));
        StepmotorUart_IsrPushByte(s_dma_rx_byte_stepmotor);
        runtime_drain_uart_rx(UART_STEPPER_BUS_INST, StepmotorUart_IsrPushByte);
        runtime_start_dma_rx(RT_DMA_STEPMOTOR_RX,
                             UART_STEPPER_BUS_INST,
                             &s_dma_rx_byte_stepmotor);
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VOFA_RX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VOFA_RX));
        VofaUart_IsrPushByte(s_dma_rx_byte_vofa);
        runtime_drain_uart_rx(UART_HOST_LINK_INST, VofaUart_IsrPushByte);
        runtime_start_dma_rx(RT_DMA_VOFA_RX, UART_HOST_LINK_INST, &s_dma_rx_byte_vofa);
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VISION_RX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VISION_RX));
        VisionUart_IsrPushByte(s_dma_rx_byte_vision);
        runtime_drain_uart_rx(UART_VISION_INST, VisionUart_IsrPushByte);
        runtime_start_dma_rx(RT_DMA_VISION_RX,
                             UART_VISION_INST,
                             &s_dma_rx_byte_vision);
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_STEPMOTOR_TX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_STEPMOTOR_TX));
        DL_UART_clearInterruptStatus(UART_STEPPER_BUS_INST,
                                     DL_UART_INTERRUPT_DMA_DONE_TX);
        StepmotorUart_IsrTxDone();
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VOFA_TX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VOFA_TX));
        DL_UART_clearInterruptStatus(UART_HOST_LINK_INST, DL_UART_INTERRUPT_DMA_DONE_TX);
        VofaUart_IsrTxDone();
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VISION_TX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VISION_TX));
        DL_UART_clearInterruptStatus(UART_VISION_INST, DL_UART_INTERRUPT_DMA_DONE_TX);
        VisionUart_IsrTxDone();
    }
}

void UART_STEPPER_BUS_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_STEPPER_BUS_INST, StepmotorUart_IsrPushByte);
}

void UART_HOST_LINK_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_HOST_LINK_INST, VofaUart_IsrPushByte);
}

void UART_VISION_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_VISION_INST, VisionUart_IsrPushByte);
}

/* UART_IMU 是唯一不走 DMA 的接收角色：帧长仅 5 字节且速率不高，
 * 逐字节中断搬运即可，无需为它占用一对 DMA 通道。 */
void UART_IMU_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_IMU_INST, ImuUart_IsrPushByte);
}

/* UART_BSL_ENTRY（UART0）RX：逐字节喂给 bsl_entry 判触发。命中 0x22 时
 * BslEntry_IsrOnByte 内部跳 BSL 永不返回（契约 D14 ISR 豁免）；否则同其他角色仅搬运。 */
void UART_BSL_ENTRY_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_BSL_ENTRY_INST, BslEntry_IsrOnByte);
}
