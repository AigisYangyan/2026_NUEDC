/**
 * @file    vision_uart.c
 * @brief   UART_VISION 角色 Driver 实现：私有 RX FIFO
 *
 * ISR/DMA 只把字节推进 FIFO，不解析。溢出计数后丢字节，不是安全事件。
 */
#include "driver/board_uart/vision_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(HOST_TEST)
#define VISION_UART_RX_FIFO_SIZE 512u
#define VISION_UART_TX_BUFFER_SIZE 32u
#else
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_uart.h>
#define VISION_UART_RX_FIFO_SIZE 512u
#define VISION_UART_TX_BUFFER_SIZE 32u
#define VISION_UART_DMA_TX_CHANNEL DMA_CH8_CHAN_ID
#endif

/* 230400 baud / 10 bits-per-byte * (2 * 5 ms service gap) * safety 2
 * = 460.8 bytes, rounded up to 512 bytes. */
typedef struct {
    uint8_t data[VISION_UART_RX_FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overflow_count;
} VisionUart_RxFifo_t;

static VisionUart_RxFifo_t s_vision_uart_fifo;
/* TX 只发选题/握手帧（0xFF main sub 0xFE，4 字节），单帧在途；
 * 32 字节缓冲远大于最长握手帧，语义与 stepmotor_uart 一致。 */
static uint8_t s_vision_uart_tx_buf[VISION_UART_TX_BUFFER_SIZE];
static volatile bool s_vision_uart_tx_busy = false;
static volatile bool s_vision_uart_tx_done = false;
static uint32_t s_vision_uart_last_tx_len = 0u;

static uint32_t vision_uart_irq_lock(void)
{
#if defined(HOST_TEST)
    return 0u;
#else
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
#endif
}

static void vision_uart_irq_unlock(uint32_t primask)
{
#if defined(HOST_TEST)
    (void)primask;
#else
    __set_PRIMASK(primask);
#endif
}

#if !defined(HOST_TEST)
static bool vision_uart_dma_tx_busy(void)
{
    return (DL_DMA_getTransferSize(DMA, VISION_UART_DMA_TX_CHANNEL) != 0u);
}
#endif

void VisionUart_Init(void)
{
    uint32_t primask = vision_uart_irq_lock();
    memset(&s_vision_uart_fifo, 0, sizeof(s_vision_uart_fifo));
    memset(s_vision_uart_tx_buf, 0, sizeof(s_vision_uart_tx_buf));
    s_vision_uart_tx_busy = false;
    s_vision_uart_tx_done = false;
    s_vision_uart_last_tx_len = 0u;
    vision_uart_irq_unlock(primask);
}

void VisionUart_IsrTxDone(void)
{
    s_vision_uart_tx_busy = false;
    s_vision_uart_tx_done = true;
}

void VisionUart_IsrPushByte(uint8_t data)
{
    if (s_vision_uart_fifo.count >= VISION_UART_RX_FIFO_SIZE) {
        s_vision_uart_fifo.overflow_count++;
        return;
    }

    s_vision_uart_fifo.data[s_vision_uart_fifo.head] = data;
    s_vision_uart_fifo.head =
        (uint16_t)((s_vision_uart_fifo.head + 1u) % VISION_UART_RX_FIFO_SIZE);
    s_vision_uart_fifo.count++;
}

uint32_t VisionUart_Read(uint8_t *out, uint32_t capacity)
{
    uint32_t read_count = 0u;
    uint32_t primask = 0u;

    if ((out == NULL) || (capacity == 0u)) {
        return 0u;
    }

    primask = vision_uart_irq_lock();
    while ((read_count < capacity) && (s_vision_uart_fifo.count > 0u)) {
        out[read_count++] = s_vision_uart_fifo.data[s_vision_uart_fifo.tail];
        s_vision_uart_fifo.tail =
            (uint16_t)((s_vision_uart_fifo.tail + 1u) % VISION_UART_RX_FIFO_SIZE);
        s_vision_uart_fifo.count--;
    }
    vision_uart_irq_unlock(primask);

    return read_count;
}

bool VisionUart_TryWrite(const uint8_t *data, uint32_t length)
{
    uint32_t primask = 0u;

    if ((data == NULL) || (length == 0u) ||
        (length > VISION_UART_TX_BUFFER_SIZE)) {
        return false;
    }

    primask = vision_uart_irq_lock();
#if !defined(HOST_TEST)
    if ((s_vision_uart_tx_busy != false) ||
        (vision_uart_dma_tx_busy() != false)) {
        vision_uart_irq_unlock(primask);
        return false;
    }
#else
    if (s_vision_uart_tx_busy != false) {
        vision_uart_irq_unlock(primask);
        return false;
    }
#endif

    memcpy(s_vision_uart_tx_buf, data, length);
    s_vision_uart_last_tx_len = length;
    s_vision_uart_tx_busy = true;
    s_vision_uart_tx_done = false;
    vision_uart_irq_unlock(primask);

#if !defined(HOST_TEST)
    DL_UART_clearInterruptStatus(UART_VISION_INST,
                                 DL_UART_INTERRUPT_DMA_DONE_TX);
    DL_DMA_disableChannel(DMA, VISION_UART_DMA_TX_CHANNEL);
    DL_DMA_setSrcAddr(DMA,
                      VISION_UART_DMA_TX_CHANNEL,
                      (uint32_t)(uintptr_t)s_vision_uart_tx_buf);
    DL_DMA_setDestAddr(DMA,
                       VISION_UART_DMA_TX_CHANNEL,
                       (uint32_t)(uintptr_t)&UART_VISION_INST->TXDATA);
    DL_DMA_setTransferSize(DMA, VISION_UART_DMA_TX_CHANNEL, (uint16_t)length);
    DL_DMA_enableChannel(DMA, VISION_UART_DMA_TX_CHANNEL);
#endif

    return true;
}

bool VisionUart_IsTxIdle(void)
{
#if defined(HOST_TEST)
    return (s_vision_uart_tx_busy == false);
#else
    return (s_vision_uart_tx_busy == false) &&
           (vision_uart_dma_tx_busy() == false);
#endif
}

bool VisionUart_ConsumeTxDone(void)
{
    bool was_done = false;
    uint32_t primask = vision_uart_irq_lock();
    was_done = (s_vision_uart_tx_done != false);
    s_vision_uart_tx_done = false;
    vision_uart_irq_unlock(primask);
    return was_done;
}

uint32_t VisionUart_GetRxOverflowCount(void)
{
    uint32_t primask = vision_uart_irq_lock();
    uint32_t overflow_count = s_vision_uart_fifo.overflow_count;
    vision_uart_irq_unlock(primask);
    return overflow_count;
}

#if defined(HOST_TEST)
void VisionUart_TestPushRxByte(uint8_t data)
{
    VisionUart_IsrPushByte(data);
}

void VisionUart_TestCompleteTx(void)
{
    VisionUart_IsrTxDone();
}

uint32_t VisionUart_TestCopyLastTx(uint8_t *out, uint32_t capacity)
{
    uint32_t copy_len = s_vision_uart_last_tx_len;

    if (copy_len > capacity) {
        copy_len = capacity;
    }

    if ((out != NULL) && (copy_len > 0u)) {
        memcpy(out, s_vision_uart_tx_buf, copy_len);
    }

    return s_vision_uart_last_tx_len;
}
#endif
