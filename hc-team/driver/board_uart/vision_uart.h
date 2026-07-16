#ifndef VISION_UART_H
#define VISION_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void VisionUart_Init(void);
uint32_t VisionUart_Read(uint8_t *out, uint32_t capacity);
uint32_t VisionUart_GetRxOverflowCount(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_UART_H */
