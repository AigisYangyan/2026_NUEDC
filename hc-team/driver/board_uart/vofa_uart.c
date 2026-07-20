/**
 * @file    vofa_uart.c
 * @brief   UART_HOST_LINK 角色 Driver 实现：私有 RX FIFO + 有界软件 TX FIFO
 *
 * ISR 链只搬字节到 FIFO；协议解析一律在 vofa_run() 任务态发生（V09）。
 * TX 侧为与 RX 对称的字节环 FIFO：TryWrite 忙时入队而非丢帧，DMA 完成中断
 * (VofaUart_IsrTxDone) 排空下一段。VOFA JustFloat/FireWater 是按尾标重同步的
 * 字节流，跨 DMA 段合并搬运合法，故 TX FIFO 不做逐帧保序，只保字节序。
 */
#include "driver/board_uart/vofa_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(HOST_TEST)
#define VOFA_UART_RX_FIFO_SIZE 512u
#define VOFA_UART_TX_RING_SIZE 512u
#define VOFA_UART_TX_BUFFER_SIZE 512u
#else
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_uart.h>
#define VOFA_UART_RX_FIFO_SIZE 512u
#define VOFA_UART_TX_RING_SIZE 512u
#define VOFA_UART_TX_BUFFER_SIZE 512u
#define VOFA_UART_DMA_TX_CHANNEL DMA_CH1_CHAN_ID
#endif

/* 一次 kick 必须能把整个 TX 环整取进 DMA 发送缓冲，故环容量 ≤ 发送缓冲容量。
 * 二者当前相等(512)，一次 kick 即整取、无需分段钳位。若日后把 VOFA_UART_TX_RING_SIZE
 * 调大到超过缓冲，此编译期断言报错，提醒补回分段排空，避免 s_vofa_uart_tx_buf 越界写。 */
typedef char vofa_uart_tx_buf_fits_ring[(VOFA_UART_TX_BUFFER_SIZE >= VOFA_UART_TX_RING_SIZE) ? 1 : -1];

/* 230400 baud / 10 bits-per-byte * (2 * 5 ms service gap) * safety 2
 * = 460.8 bytes, rounded up to 512 bytes. */
typedef struct {
    uint8_t data[VOFA_UART_RX_FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overflow_count;
} VofaUart_RxFifo_t;

/* TX 软件队列：忙时入队的字节在 DMA 在途期间存放于此，完成中断把下一段搬进
 * s_vofa_uart_tx_buf 再发。容量与 RX 对称（512B），可吸收发送节拍与 DMA 完成的抖动。 */
typedef struct {
    uint8_t data[VOFA_UART_TX_RING_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overflow_count;
} VofaUart_TxFifo_t;

static VofaUart_RxFifo_t s_vofa_uart_fifo;
static VofaUart_TxFifo_t s_vofa_uart_tx_fifo;
/* DMA 发送源缓冲：每次从 TX 环里搬出一段连续字节到这里，交给 DMA 发。 */
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

/* 若 DMA 空闲且队列非空，则把队首一段连续字节搬进发送缓冲并启动 DMA。
 * 调用者须已屏蔽中断（TryWrite 持锁 / IsrTxDone 在中断上下文），保证与
 * DMA 完成中断互斥。busy 标志是 DMA 忙的唯一权威：kick 置位、IsrTxDone 清位。 */
static void vofa_uart_tx_kick(void)
{
    uint16_t chunk = 0u;
    uint16_t i = 0u;

    if ((s_vofa_uart_tx_busy != false) || (s_vofa_uart_tx_fifo.count == 0u)) {
        return;
    }

    chunk = s_vofa_uart_tx_fifo.count;

    for (i = 0u; i < chunk; i++) {
        s_vofa_uart_tx_buf[i] = s_vofa_uart_tx_fifo.data[s_vofa_uart_tx_fifo.tail];
        s_vofa_uart_tx_fifo.tail =
            (uint16_t)((s_vofa_uart_tx_fifo.tail + 1u) % VOFA_UART_TX_RING_SIZE);
    }
    s_vofa_uart_tx_fifo.count = (uint16_t)(s_vofa_uart_tx_fifo.count - chunk);
    s_vofa_uart_last_tx_len = chunk;
    s_vofa_uart_tx_busy = true;

#if !defined(HOST_TEST)
    DL_UART_clearInterruptStatus(UART_HOST_LINK_INST, DL_UART_INTERRUPT_DMA_DONE_TX);
    DL_DMA_disableChannel(DMA, VOFA_UART_DMA_TX_CHANNEL);
    DL_DMA_setSrcAddr(DMA,
                      VOFA_UART_DMA_TX_CHANNEL,
                      (uint32_t)(uintptr_t)s_vofa_uart_tx_buf);
    DL_DMA_setDestAddr(DMA,
                       VOFA_UART_DMA_TX_CHANNEL,
                       (uint32_t)(uintptr_t)&UART_HOST_LINK_INST->TXDATA);
    DL_DMA_setTransferSize(DMA, VOFA_UART_DMA_TX_CHANNEL, chunk);
    DL_DMA_enableChannel(DMA, VOFA_UART_DMA_TX_CHANNEL);
#endif
}

void VofaUart_Init(void)
{
    uint32_t primask = vofa_uart_irq_lock();
    memset(&s_vofa_uart_fifo, 0, sizeof(s_vofa_uart_fifo));
    memset(&s_vofa_uart_tx_fifo, 0, sizeof(s_vofa_uart_tx_fifo));
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
    vofa_uart_tx_kick();
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
    uint32_t i = 0u;

    if ((data == NULL) || (length == 0u) ||
        (length > VOFA_UART_TX_RING_SIZE)) {
        return false;
    }

    primask = vofa_uart_irq_lock();
    if (((uint32_t)s_vofa_uart_tx_fifo.count + length) > VOFA_UART_TX_RING_SIZE) {
        s_vofa_uart_tx_fifo.overflow_count++;
        vofa_uart_irq_unlock(primask);
        return false;
    }

    for (i = 0u; i < length; i++) {
        s_vofa_uart_tx_fifo.data[s_vofa_uart_tx_fifo.head] = data[i];
        s_vofa_uart_tx_fifo.head =
            (uint16_t)((s_vofa_uart_tx_fifo.head + 1u) % VOFA_UART_TX_RING_SIZE);
    }
    s_vofa_uart_tx_fifo.count = (uint16_t)(s_vofa_uart_tx_fifo.count + length);

    vofa_uart_tx_kick();
    vofa_uart_irq_unlock(primask);

    return true;
}

uint32_t VofaUart_GetRxOverflowCount(void)
{
    uint32_t primask = vofa_uart_irq_lock();
    uint32_t overflow_count = s_vofa_uart_fifo.overflow_count;
    vofa_uart_irq_unlock(primask);
    return overflow_count;
}

uint32_t VofaUart_GetTxOverflowCount(void)
{
    uint32_t primask = vofa_uart_irq_lock();
    uint32_t overflow_count = s_vofa_uart_tx_fifo.overflow_count;
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
