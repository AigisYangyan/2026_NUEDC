#ifndef VOFA_UART_H
#define VOFA_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void VofaUart_Init(void);
uint32_t VofaUart_Read(uint8_t *out, uint32_t capacity);
bool VofaUart_TryWrite(const uint8_t *data, uint32_t length);
uint32_t VofaUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* VOFA_UART_H */
