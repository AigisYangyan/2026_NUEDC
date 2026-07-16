#include "driver/board_uart/vision_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(HOST_TEST)
#define VISION_UART_RX_FIFO_SIZE 512u
#else
#include "ti_msp_dl_config.h"
#define VISION_UART_RX_FIFO_SIZE 512u
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

void VisionUart_Init(void)
{
    uint32_t primask = vision_uart_irq_lock();
    memset(&s_vision_uart_fifo, 0, sizeof(s_vision_uart_fifo));
    vision_uart_irq_unlock(primask);
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
#endif
