#ifndef STEPMOTOR_UART_H
#define STEPMOTOR_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void StepmotorUart_Init(void);
uint32_t StepmotorUart_Read(uint8_t *out, uint32_t capacity);
bool StepmotorUart_TryWrite(const uint8_t *data, uint32_t length);
bool StepmotorUart_IsTxIdle(void);
bool StepmotorUart_ConsumeTxDone(void);
uint32_t StepmotorUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* STEPMOTOR_UART_H */
