#ifndef IMU_UART_H
#define IMU_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ImuUart_Init(void);
bool ImuUart_TryWrite(const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* IMU_UART_H */
