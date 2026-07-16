/**
 * @file    mspm0_runtime.c
 * @brief   Board-specific MSPM0G3519 runtime: shared GPIO IRQ, UART/DMA
 *          dispatch, board-role UART DMA TX send, and hardware QEI readout.
 *
 * Owns GROUP1 (keys), DMA, and the three board UART IRQ symbols.
 * Encoder counting moved from GROUP1 software edge-counting (G3507) to the
 * two hardware QEI timers (G3519: QEI_LEFT=TIMG8, QEI_RIGHT=TIMG9).
 * SysTick is now owned by driver/clock.
 */
#include "driver/mspm0_runtime/mspm0_runtime.h"

#include "driver/clock/clock.h"
#include "ti_msp_dl_config.h"

#include <string.h>
#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_gpio.h>
#include <ti/driverlib/dl_timerg.h>
#include <ti/driverlib/dl_uart.h>

/* Delay and DMA constants. SysTick is owned by driver/clock. */
#define MSPM0_RUNTIME_DMA_TX_BUFFER_SIZE    512u

/* SysConfig DMA channel IDs for the three board UART roles. */
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

static Mspm0Runtime_UartRxCallback s_stepmotor_rx_cb = NULL;
static Mspm0Runtime_UartRxCallback s_vofa_rx_cb = NULL;
static Mspm0Runtime_UartRxCallback s_vision_rx_cb = NULL;
static Mspm0Runtime_UartTxCallback s_stepmotor_tx_cb = NULL;

static uint8_t s_dma_rx_byte_stepmotor;
static uint8_t s_dma_rx_byte_vofa;
static uint8_t s_dma_rx_byte_vision;

static uint8_t s_dma_tx_buf_stepmotor[MSPM0_RUNTIME_DMA_TX_BUFFER_SIZE];
static uint8_t s_dma_tx_buf_vofa[MSPM0_RUNTIME_DMA_TX_BUFFER_SIZE];

static volatile bool s_tx_busy_stepmotor = false;
static volatile bool s_tx_busy_vofa = false;
static volatile bool s_tx_busy_vision = false;

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

static void runtime_dispatch_rx(Mspm0Runtime_UartRxCallback cb, uint8_t data)
{
    if (cb != NULL) {
        cb(data);
    }
}

static void runtime_drain_uart_rx(UART_Regs *uart, Mspm0Runtime_UartRxCallback cb)
{
    uint8_t data = 0u;

    while (DL_UART_receiveDataCheck(uart, &data)) {
        runtime_dispatch_rx(cb, data);
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

static bool runtime_dma_channel_busy(uint8_t dma_ch)
{
    return (DL_DMA_getTransferSize(DMA, dma_ch) != 0u);
}

static bool runtime_uart_send_dma(UART_Regs *uart,
                                  uint8_t dma_ch,
                                  volatile bool *busy_flag,
                                  uint8_t *tx_buf,
                                  const uint8_t *data,
                                  uint32_t length)
{
    uint32_t i;

    if ((uart == NULL) || (busy_flag == NULL) || (tx_buf == NULL) ||
        (data == NULL) || (length == 0u)) {
        return false;
    }

    if ((*busy_flag != false) || (runtime_dma_channel_busy(dma_ch) != false)) {
        return false;
    }

    if (length <= MSPM0_RUNTIME_DMA_TX_BUFFER_SIZE) {
        (void)memcpy(tx_buf, data, length);
        *busy_flag = true;
        DL_UART_clearInterruptStatus(uart, DL_UART_INTERRUPT_DMA_DONE_TX);
        DL_DMA_disableChannel(DMA, dma_ch);
        DL_DMA_setSrcAddr(DMA, dma_ch, (uint32_t)(uintptr_t)tx_buf);
        DL_DMA_setDestAddr(DMA, dma_ch, (uint32_t)(uintptr_t)&uart->TXDATA);
        DL_DMA_setTransferSize(DMA, dma_ch, (uint16_t)length);
        DL_DMA_enableChannel(DMA, dma_ch);
        return true;
    }

    for (i = 0u; i < length; i++) {
        DL_UART_transmitDataBlocking(uart, data[i]);
    }
    return true;
}

static bool runtime_uart_send_byte(UART_Regs *uart, uint8_t data)
{
    if (uart == NULL) {
        return false;
    }
    DL_UART_transmitDataBlocking(uart, data);
    return true;
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

static void runtime_handle_uart_irq(UART_Regs *uart, Mspm0Runtime_UartRxCallback cb)
{
    uint32_t irq_status;

    irq_status = DL_UART_getEnabledInterruptStatus(
        uart,
        DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_DMA_DONE_RX |
            DL_UART_INTERRUPT_DMA_DONE_TX);

    if ((irq_status & DL_UART_INTERRUPT_RX) != 0u) {
        runtime_drain_uart_rx(uart, cb);
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
    uint32_t i;

    for (i = 0u; i < (sizeof(dma_channels) / sizeof(dma_channels[0])); i++) {
        uint32_t irq_mask = runtime_dma_irq_mask(dma_channels[i]);
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
    uint32_t start_ms;
    uint32_t now_ms;

    if (delay_ms == 0u) {
        return;
    }

    start_ms = Clock_NowMs();
    do {
        now_ms = Clock_NowMs();
    } while ((now_ms - start_ms) < delay_ms);
}

void Mspm0Runtime_SetStepmotorRxCallback(Mspm0Runtime_UartRxCallback callback)
{
    s_stepmotor_rx_cb = callback;
}

void Mspm0Runtime_SetVofaRxCallback(Mspm0Runtime_UartRxCallback callback)
{
    s_vofa_rx_cb = callback;
}

void Mspm0Runtime_SetVisionRxCallback(Mspm0Runtime_UartRxCallback callback)
{
    s_vision_rx_cb = callback;
}

void Mspm0Runtime_SetStepmotorTxCallback(Mspm0Runtime_UartTxCallback callback)
{
    s_stepmotor_tx_cb = callback;
}

bool Mspm0Runtime_IsStepmotorTxBusy(void)
{
    return (s_tx_busy_stepmotor != false) ||
           runtime_dma_channel_busy(RT_DMA_STEPMOTOR_TX);
}

bool Mspm0Runtime_SendStepmotor(const uint8_t *data, uint32_t length)
{
    return runtime_uart_send_dma(UART_STEPPER_BUS_INST,
                                 RT_DMA_STEPMOTOR_TX,
                                 &s_tx_busy_stepmotor,
                                 s_dma_tx_buf_stepmotor,
                                 data,
                                 length);
}

bool Mspm0Runtime_SendVofa(const uint8_t *data, uint32_t length)
{
    return runtime_uart_send_dma(UART_HOST_LINK_INST,
                                 RT_DMA_VOFA_TX,
                                 &s_tx_busy_vofa,
                                 s_dma_tx_buf_vofa,
                                 data,
                                 length);
}

bool Mspm0Runtime_SendStepmotorByte(uint8_t data)
{
    return runtime_uart_send_byte(UART_STEPPER_BUS_INST, data);
}

void Mspm0Runtime_GetEncoderCounts(int32_t *left, int32_t *right)
{
    int32_t local_left;
    int32_t local_right;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    runtime_qei_accumulate((uint16_t)DL_TimerG_getTimerCount(QEI_LEFT_INST),
                           &s_qei_last_left,
                           &s_left_encoder_count);
    runtime_qei_accumulate((uint16_t)DL_TimerG_getTimerCount(QEI_RIGHT_INST),
                           &s_qei_last_right,
                           &s_right_encoder_count);
    local_left  = s_left_encoder_count;
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
    uint8_t key_edges;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    key_edges = s_key_irq_edges;
    s_key_irq_edges = 0u;
    __set_PRIMASK(primask);

    return key_edges;
}

void GROUP1_IRQHandler(void)
{
    uint32_t pending_a;
    uint32_t pending_b;

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
    Mspm0Runtime_UartTxCallback tx_cb;

    if (pending_irq == 0u) {
        return;
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_STEPMOTOR_RX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_STEPMOTOR_RX));
        runtime_dispatch_rx(s_stepmotor_rx_cb, s_dma_rx_byte_stepmotor);
        runtime_drain_uart_rx(UART_STEPPER_BUS_INST, s_stepmotor_rx_cb);
        runtime_start_dma_rx(RT_DMA_STEPMOTOR_RX,
                             UART_STEPPER_BUS_INST,
                             &s_dma_rx_byte_stepmotor);
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VOFA_RX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VOFA_RX));
        runtime_dispatch_rx(s_vofa_rx_cb, s_dma_rx_byte_vofa);
        runtime_drain_uart_rx(UART_HOST_LINK_INST, s_vofa_rx_cb);
        runtime_start_dma_rx(RT_DMA_VOFA_RX, UART_HOST_LINK_INST, &s_dma_rx_byte_vofa);
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VISION_RX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VISION_RX));
        runtime_dispatch_rx(s_vision_rx_cb, s_dma_rx_byte_vision);
        runtime_drain_uart_rx(UART_VISION_INST, s_vision_rx_cb);
        runtime_start_dma_rx(RT_DMA_VISION_RX,
                             UART_VISION_INST,
                             &s_dma_rx_byte_vision);
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_STEPMOTOR_TX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_STEPMOTOR_TX));
        DL_UART_clearInterruptStatus(UART_STEPPER_BUS_INST,
                                     DL_UART_INTERRUPT_DMA_DONE_TX);
        s_tx_busy_stepmotor = false;
        tx_cb = s_stepmotor_tx_cb;
        if (tx_cb != NULL) {
            tx_cb();
        }
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VOFA_TX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VOFA_TX));
        DL_UART_clearInterruptStatus(UART_HOST_LINK_INST, DL_UART_INTERRUPT_DMA_DONE_TX);
        s_tx_busy_vofa = false;
    }

    if ((pending_irq & runtime_dma_irq_mask(RT_DMA_VISION_TX)) != 0u) {
        DL_DMA_clearInterruptStatus(DMA, runtime_dma_irq_mask(RT_DMA_VISION_TX));
        DL_UART_clearInterruptStatus(UART_VISION_INST, DL_UART_INTERRUPT_DMA_DONE_TX);
        s_tx_busy_vision = false;
    }
}

void UART_STEPPER_BUS_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_STEPPER_BUS_INST, s_stepmotor_rx_cb);
}

void UART_HOST_LINK_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_HOST_LINK_INST, s_vofa_rx_cb);
}

void UART_VISION_INST_IRQHandler(void)
{
    runtime_handle_uart_irq(UART_VISION_INST, s_vision_rx_cb);
}
