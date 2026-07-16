#include "driver/board_uart/imu_uart.h"

#include <stdbool.h>
#include <stddef.h>

#if !defined(HOST_TEST)
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_uart.h>
#endif

void ImuUart_Init(void)
{
    /* Keep the dormant IMU TX role linked into the firmware image without
     * touching hardware state: zero-length writes return false immediately. */
    (void)ImuUart_TryWrite(NULL, 0u);
}

bool ImuUart_TryWrite(const uint8_t *data, uint32_t length)
{
#if defined(HOST_TEST)
    (void)data;
    return (length > 0u);
#else
    uint32_t index = 0u;
    uint32_t byte_deadline_cycles = 0u;

    if ((data == NULL) || (length == 0u)) {
        return false;
    }

    /* UART_IMU runs at 230400 baud from board.syscfg. One byte takes
     * 10 / 230400 s = 43.4 us. The bounded poll waits 2 byte-times per byte,
     * rounded up to 87 us -> about 6960 cycles at the 80 MHz system clock. */
    byte_deadline_cycles = ((80u * 2u * 10u * 1000u) + 230400u - 1u) / 230400u;

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
#endif
}
