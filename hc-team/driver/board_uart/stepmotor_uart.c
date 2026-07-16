#include "driver/board_uart/stepmotor_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(HOST_TEST)
#define STEPMOTOR_UART_RX_FIFO_SIZE 256u
#define STEPMOTOR_UART_TX_BUFFER_SIZE 32u
#else
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_uart.h>
#define STEPMOTOR_UART_RX_FIFO_SIZE 256u
#define STEPMOTOR_UART_TX_BUFFER_SIZE 32u
#define STEPMOTOR_UART_DMA_TX_CHANNEL DMA_CH3_CHAN_ID
#endif

/* Command/response traffic keeps a single frame in flight, but the role still
 * reserves 230400 / 10 bits-per-byte * 5 ms * safety 2 = 230.4 bytes, rounded
 * up to 256 bytes, so one missed 5 ms service gap cannot drop valid replies. */
typedef struct {
    uint8_t data[STEPMOTOR_UART_RX_FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overflow_count;
} StepmotorUart_RxFifo_t;

static StepmotorUart_RxFifo_t s_stepmotor_uart_fifo;
static uint8_t s_stepmotor_uart_tx_buf[STEPMOTOR_UART_TX_BUFFER_SIZE];
static volatile bool s_stepmotor_uart_tx_busy = false;
static volatile bool s_stepmotor_uart_tx_done = false;
static uint32_t s_stepmotor_uart_last_tx_len = 0u;

static uint32_t stepmotor_uart_irq_lock(void)
{
#if defined(HOST_TEST)
    return 0u;
#else
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
#endif
}

static void stepmotor_uart_irq_unlock(uint32_t primask)
{
#if defined(HOST_TEST)
    (void)primask;
#else
    __set_PRIMASK(primask);
#endif
}

#if !defined(HOST_TEST)
static bool stepmotor_uart_dma_tx_busy(void)
{
    return (DL_DMA_getTransferSize(DMA, STEPMOTOR_UART_DMA_TX_CHANNEL) != 0u);
}
#endif

void StepmotorUart_Init(void)
{
    uint32_t primask = stepmotor_uart_irq_lock();
    memset(&s_stepmotor_uart_fifo, 0, sizeof(s_stepmotor_uart_fifo));
    memset(s_stepmotor_uart_tx_buf, 0, sizeof(s_stepmotor_uart_tx_buf));
    s_stepmotor_uart_tx_busy = false;
    s_stepmotor_uart_tx_done = false;
    s_stepmotor_uart_last_tx_len = 0u;
    stepmotor_uart_irq_unlock(primask);
}

void StepmotorUart_IsrPushByte(uint8_t data)
{
    if (s_stepmotor_uart_fifo.count >= STEPMOTOR_UART_RX_FIFO_SIZE) {
        s_stepmotor_uart_fifo.overflow_count++;
        return;
    }

    s_stepmotor_uart_fifo.data[s_stepmotor_uart_fifo.head] = data;
    s_stepmotor_uart_fifo.head =
        (uint16_t)((s_stepmotor_uart_fifo.head + 1u) % STEPMOTOR_UART_RX_FIFO_SIZE);
    s_stepmotor_uart_fifo.count++;
}

void StepmotorUart_IsrTxDone(void)
{
    s_stepmotor_uart_tx_busy = false;
    s_stepmotor_uart_tx_done = true;
}

uint32_t StepmotorUart_Read(uint8_t *out, uint32_t capacity)
{
    uint32_t read_count = 0u;
    uint32_t primask = 0u;

    if ((out == NULL) || (capacity == 0u)) {
        return 0u;
    }

    primask = stepmotor_uart_irq_lock();
    while ((read_count < capacity) && (s_stepmotor_uart_fifo.count > 0u)) {
        out[read_count++] = s_stepmotor_uart_fifo.data[s_stepmotor_uart_fifo.tail];
        s_stepmotor_uart_fifo.tail =
            (uint16_t)((s_stepmotor_uart_fifo.tail + 1u) % STEPMOTOR_UART_RX_FIFO_SIZE);
        s_stepmotor_uart_fifo.count--;
    }
    stepmotor_uart_irq_unlock(primask);

    return read_count;
}

bool StepmotorUart_TryWrite(const uint8_t *data, uint32_t length)
{
    uint32_t primask = 0u;

    if ((data == NULL) || (length == 0u) ||
        (length > STEPMOTOR_UART_TX_BUFFER_SIZE)) {
        return false;
    }

    primask = stepmotor_uart_irq_lock();
#if !defined(HOST_TEST)
    if ((s_stepmotor_uart_tx_busy != false) ||
        (stepmotor_uart_dma_tx_busy() != false)) {
        stepmotor_uart_irq_unlock(primask);
        return false;
    }
#else
    if (s_stepmotor_uart_tx_busy != false) {
        stepmotor_uart_irq_unlock(primask);
        return false;
    }
#endif

    memcpy(s_stepmotor_uart_tx_buf, data, length);
    s_stepmotor_uart_last_tx_len = length;
    s_stepmotor_uart_tx_busy = true;
    s_stepmotor_uart_tx_done = false;
    stepmotor_uart_irq_unlock(primask);

#if !defined(HOST_TEST)
    DL_UART_clearInterruptStatus(UART_STEPPER_BUS_INST,
                                 DL_UART_INTERRUPT_DMA_DONE_TX);
    DL_DMA_disableChannel(DMA, STEPMOTOR_UART_DMA_TX_CHANNEL);
    DL_DMA_setSrcAddr(DMA,
                      STEPMOTOR_UART_DMA_TX_CHANNEL,
                      (uint32_t)(uintptr_t)s_stepmotor_uart_tx_buf);
    DL_DMA_setDestAddr(DMA,
                       STEPMOTOR_UART_DMA_TX_CHANNEL,
                       (uint32_t)(uintptr_t)&UART_STEPPER_BUS_INST->TXDATA);
    DL_DMA_setTransferSize(DMA, STEPMOTOR_UART_DMA_TX_CHANNEL, (uint16_t)length);
    DL_DMA_enableChannel(DMA, STEPMOTOR_UART_DMA_TX_CHANNEL);
#endif

    return true;
}

bool StepmotorUart_IsTxIdle(void)
{
#if defined(HOST_TEST)
    return (s_stepmotor_uart_tx_busy == false);
#else
    return (s_stepmotor_uart_tx_busy == false) &&
           (stepmotor_uart_dma_tx_busy() == false);
#endif
}

bool StepmotorUart_ConsumeTxDone(void)
{
    bool was_done = false;
    uint32_t primask = stepmotor_uart_irq_lock();
    was_done = (s_stepmotor_uart_tx_done != false);
    s_stepmotor_uart_tx_done = false;
    stepmotor_uart_irq_unlock(primask);
    return was_done;
}

uint32_t StepmotorUart_GetRxOverflowCount(void)
{
    uint32_t primask = stepmotor_uart_irq_lock();
    uint32_t overflow_count = s_stepmotor_uart_fifo.overflow_count;
    stepmotor_uart_irq_unlock(primask);
    return overflow_count;
}

#if defined(HOST_TEST)
void StepmotorUart_TestPushRxByte(uint8_t data)
{
    StepmotorUart_IsrPushByte(data);
}

void StepmotorUart_TestCompleteTx(void)
{
    StepmotorUart_IsrTxDone();
}

uint32_t StepmotorUart_TestCopyLastTx(uint8_t *out, uint32_t capacity)
{
    uint32_t copy_len = s_stepmotor_uart_last_tx_len;

    if (copy_len > capacity) {
        copy_len = capacity;
    }

    if ((out != NULL) && (copy_len > 0u)) {
        memcpy(out, s_stepmotor_uart_tx_buf, copy_len);
    }

    return s_stepmotor_uart_last_tx_len;
}
#endif
