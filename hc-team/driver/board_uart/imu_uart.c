#include "driver/board_uart/imu_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if !defined(HOST_TEST)
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_uart.h>
#endif

/* 器件在 200 Hz 输出速率下发两种帧共 2000 B/s。128 字节可容纳约 64 ms 的数据，
 * 远宽于任何合理的 Imu_Update() 周期。溢出不是安全事件：计数后丢字节，
 * 解析器靠滑动窗口自行重同步。 */
#define IMU_UART_RX_FIFO_SIZE 128u

typedef struct {
    uint8_t data[IMU_UART_RX_FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overflow_count;
} ImuUart_RxFifo_t;

static ImuUart_RxFifo_t s_imu_uart_fifo;

#if defined(HOST_TEST)
/* 主机测试需要按顺序核对整条写指令序列（解锁→设置→保存共 15 字节），
 * 故按追加方式记录，而非只保留最后一帧。 */
#define IMU_UART_TX_LOG_SIZE 64u
static uint8_t s_imu_uart_tx_log[IMU_UART_TX_LOG_SIZE];
static uint32_t s_imu_uart_tx_log_len;
#endif

static uint32_t imu_uart_irq_lock(void)
{
#if defined(HOST_TEST)
    return 0u;
#else
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
#endif
}

static void imu_uart_irq_unlock(uint32_t primask)
{
#if defined(HOST_TEST)
    (void)primask;
#else
    __set_PRIMASK(primask);
#endif
}

void ImuUart_Init(void)
{
    uint32_t primask = imu_uart_irq_lock();
    memset(&s_imu_uart_fifo, 0, sizeof(s_imu_uart_fifo));
#if defined(HOST_TEST)
    memset(s_imu_uart_tx_log, 0, sizeof(s_imu_uart_tx_log));
    s_imu_uart_tx_log_len = 0u;
#endif
    imu_uart_irq_unlock(primask);
}

void ImuUart_IsrPushByte(uint8_t data)
{
    if (s_imu_uart_fifo.count >= IMU_UART_RX_FIFO_SIZE) {
        s_imu_uart_fifo.overflow_count++;
        return;
    }

    s_imu_uart_fifo.data[s_imu_uart_fifo.head] = data;
    s_imu_uart_fifo.head =
        (uint16_t)((s_imu_uart_fifo.head + 1u) % IMU_UART_RX_FIFO_SIZE);
    s_imu_uart_fifo.count++;
}

uint32_t ImuUart_Read(uint8_t *out, uint32_t capacity)
{
    uint32_t read_count = 0u;
    uint32_t primask = 0u;

    if ((out == NULL) || (capacity == 0u)) {
        return 0u;
    }

    primask = imu_uart_irq_lock();
    while ((read_count < capacity) && (s_imu_uart_fifo.count > 0u)) {
        out[read_count++] = s_imu_uart_fifo.data[s_imu_uart_fifo.tail];
        s_imu_uart_fifo.tail =
            (uint16_t)((s_imu_uart_fifo.tail + 1u) % IMU_UART_RX_FIFO_SIZE);
        s_imu_uart_fifo.count--;
    }
    imu_uart_irq_unlock(primask);

    return read_count;
}

uint32_t ImuUart_GetRxOverflowCount(void)
{
    uint32_t primask = imu_uart_irq_lock();
    uint32_t overflow_count = s_imu_uart_fifo.overflow_count;
    imu_uart_irq_unlock(primask);
    return overflow_count;
}

bool ImuUart_TryWrite(const uint8_t *data, uint32_t length)
{
    if ((data == NULL) || (length == 0u)) {
        return false;
    }

#if defined(HOST_TEST)
    if ((s_imu_uart_tx_log_len + length) <= IMU_UART_TX_LOG_SIZE) {
        memcpy(&s_imu_uart_tx_log[s_imu_uart_tx_log_len], data, length);
        s_imu_uart_tx_log_len += length;
    }
    return true;
#else
    {
        uint32_t index = 0u;
        /* UART_IMU 为 115200 baud（board.syscfg 单源）。一个字节 10/115200 s
         * = 86.8 us。每字节的有界轮询等 2 个字节时间，向上取整 174 us
         * -> 80 MHz 系统时钟下约 13920 个周期。 */
        uint32_t byte_deadline_cycles =
            ((80u * 2u * 10u * 1000u) + 115200u - 1u) / 115200u;

        for (index = 0u; index < length; index++) {
            uint32_t remaining_cycles = byte_deadline_cycles;

            while ((DL_UART_isTXFIFOFull(UART_IMU_INST) != false) &&
                   (remaining_cycles > 0u)) {
                delay_cycles(1u);
                remaining_cycles--;
            }

            if (DL_UART_isTXFIFOFull(UART_IMU_INST) != false) {
                return false;
            }

            DL_UART_transmitData(UART_IMU_INST, data[index]);
        }

        return true;
    }
#endif
}

#if defined(HOST_TEST)
void ImuUart_TestPushRxByte(uint8_t data)
{
    ImuUart_IsrPushByte(data);
}

uint32_t ImuUart_TestCopyTxLog(uint8_t *out, uint32_t capacity)
{
    uint32_t copy_len = s_imu_uart_tx_log_len;

    if (copy_len > capacity) {
        copy_len = capacity;
    }

    if ((out != NULL) && (copy_len > 0u)) {
        memcpy(out, s_imu_uart_tx_log, copy_len);
    }

    return s_imu_uart_tx_log_len;
}
#endif
