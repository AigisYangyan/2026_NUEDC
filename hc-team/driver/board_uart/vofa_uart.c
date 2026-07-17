/**
 * @file    vofa_uart.c
 * @brief   UART_HOST_LINK 角色 Driver 实现：私有 RX FIFO + 有界 TX
 *
 * ISR 链只搬字节到 FIFO；协议解析一律在 vofa_run() 任务态发生（V09）。
 */
#include "driver/board_uart/vofa_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(HOST_TEST)
#define VOFA_UART_RX_FIFO_SIZE 512u
#define VOFA_UART_TX_BUFFER_SIZE 512u
#else
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_uart.h>
#define VOFA_UART_RX_FIFO_SIZE 512u
#define VOFA_UART_TX_BUFFER_SIZE 512u
#define VOFA_UART_DMA_TX_CHANNEL DMA_CH1_CHAN_ID
#endif

/* 230400 baud / 10 bits-per-byte * (2 * 5 ms service gap) * safety 2
 * = 460.8 bytes, rounded up to 512 bytes. */
typedef struct {
    uint8_t data[VOFA_UART_RX_FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overflow_count;
} VofaUart_RxFifo_t;

static VofaUart_RxFifo_t s_vofa_uart_fifo;
static uint8_t s_vofa_uart_tx_buf[VOFA_UART_TX_BUFFER_SIZE];
static volatile bool s_vofa_uart_tx_busy = false;
static uint32_t s_vofa_uart_last_tx_len = 0u;

static uint32_t vofa_uart_irq_lock(void)
{
#if defined(HOST_TEST)
    return 0u;
#else
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
#endif
}

static void vofa_uart_irq_unlock(uint32_t primask)
{
#if defined(HOST_TEST)
    (void)primask;
#else
    __set_PRIMASK(primask);
#endif
}

#if !defined(HOST_TEST)
static bool vofa_uart_dma_tx_busy(void)
{
    return (DL_DMA_getTransferSize(DMA, VOFA_UART_DMA_TX_CHANNEL) != 0u);
}
#endif

void VofaUart_Init(void)
{
    uint32_t primask = vofa_uart_irq_lock();
    memset(&s_vofa_uart_fifo, 0, sizeof(s_vofa_uart_fifo));
    memset(s_vofa_uart_tx_buf, 0, sizeof(s_vofa_uart_tx_buf));
    s_vofa_uart_tx_busy = false;
    s_vofa_uart_last_tx_len = 0u;
    vofa_uart_irq_unlock(primask);
}

void VofaUart_IsrPushByte(uint8_t data)
{
    if (s_vofa_uart_fifo.count >= VOFA_UART_RX_FIFO_SIZE) {
        s_vofa_uart_fifo.overflow_count++;
        return;
    }

    s_vofa_uart_fifo.data[s_vofa_uart_fifo.head] = data;
    s_vofa_uart_fifo.head =
        (uint16_t)((s_vofa_uart_fifo.head + 1u) % VOFA_UART_RX_FIFO_SIZE);
    s_vofa_uart_fifo.count++;
}

void VofaUart_IsrTxDone(void)
{
    s_vofa_uart_tx_busy = false;
}

uint32_t VofaUart_Read(uint8_t *out, uint32_t capacity)
{
    uint32_t read_count = 0u;
    uint32_t primask = 0u;

    if ((out == NULL) || (capacity == 0u)) {
        return 0u;
    }

    primask = vofa_uart_irq_lock();
    while ((read_count < capacity) && (s_vofa_uart_fifo.count > 0u)) {
        out[read_count++] = s_vofa_uart_fifo.data[s_vofa_uart_fifo.tail];
        s_vofa_uart_fifo.tail =
            (uint16_t)((s_vofa_uart_fifo.tail + 1u) % VOFA_UART_RX_FIFO_SIZE);
        s_vofa_uart_fifo.count--;
    }
    vofa_uart_irq_unlock(primask);

    return read_count;
}

bool VofaUart_TryWrite(const uint8_t *data, uint32_t length)
{
    uint32_t primask = 0u;

    if ((data == NULL) || (length == 0u) ||
        (length > VOFA_UART_TX_BUFFER_SIZE)) {
        return false;
    }

    primask = vofa_uart_irq_lock();
#if !defined(HOST_TEST)
    if ((s_vofa_uart_tx_busy != false) || (vofa_uart_dma_tx_busy() != false)) {
        vofa_uart_irq_unlock(primask);
        return false;
    }
#else
    if (s_vofa_uart_tx_busy != false) {
        vofa_uart_irq_unlock(primask);
        return false;
    }
#endif

    memcpy(s_vofa_uart_tx_buf, data, length);
    s_vofa_uart_last_tx_len = length;
    s_vofa_uart_tx_busy = true;
    vofa_uart_irq_unlock(primask);

#if !defined(HOST_TEST)
    DL_UART_clearInterruptStatus(UART_HOST_LINK_INST, DL_UART_INTERRUPT_DMA_DONE_TX);
    DL_DMA_disableChannel(DMA, VOFA_UART_DMA_TX_CHANNEL);
    DL_DMA_setSrcAddr(DMA,
                      VOFA_UART_DMA_TX_CHANNEL,
                      (uint32_t)(uintptr_t)s_vofa_uart_tx_buf);
    DL_DMA_setDestAddr(DMA,
                       VOFA_UART_DMA_TX_CHANNEL,
                       (uint32_t)(uintptr_t)&UART_HOST_LINK_INST->TXDATA);
    DL_DMA_setTransferSize(DMA, VOFA_UART_DMA_TX_CHANNEL, (uint16_t)length);
    DL_DMA_enableChannel(DMA, VOFA_UART_DMA_TX_CHANNEL);
#endif

    return true;
}

uint32_t VofaUart_GetRxOverflowCount(void)
{
    uint32_t primask = vofa_uart_irq_lock();
    uint32_t overflow_count = s_vofa_uart_fifo.overflow_count;
    vofa_uart_irq_unlock(primask);
    return overflow_count;
}

#if defined(HOST_TEST)
void VofaUart_TestPushRxByte(uint8_t data)
{
    VofaUart_IsrPushByte(data);
}

void VofaUart_TestCompleteTx(void)
{
    VofaUart_IsrTxDone();
}

uint32_t VofaUart_TestCopyLastTx(uint8_t *out, uint32_t capacity)
{
    uint32_t copy_len = s_vofa_uart_last_tx_len;

    if (copy_len > capacity) {
        copy_len = capacity;
    }

    if ((out != NULL) && (copy_len > 0u)) {
        memcpy(out, s_vofa_uart_tx_buf, copy_len);
    }

    return s_vofa_uart_last_tx_len;
}
#endif
