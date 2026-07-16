/**
 * @file    mspm0_runtime.h
 * @brief   Board-specific MSPM0G3519 runtime boundary for UART/DMA, shared GPIO IRQ state, and QEI readout.
 *
 * This is not a reusable HAL. Hardware details stay in mspm0_runtime.c.
 * Public APIs use only standard C types.
 */
#ifndef MSPM0_RUNTIME_H
#define MSPM0_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Mspm0Runtime_UartRxCallback)(uint8_t data);
typedef void (*Mspm0Runtime_UartTxCallback)(void);

/**
 * @brief Initialize UART/DMA dispatch state used by board UART roles.
 * @note  Owns DMA/UART IRQ dispatch, RX rearm, and board-role DMA TX send.
 */
void Mspm0Runtime_InitUartDma(void);

void Mspm0Runtime_DelayMs(uint32_t delay_ms);

void Mspm0Runtime_SetStepmotorRxCallback(Mspm0Runtime_UartRxCallback callback);
void Mspm0Runtime_SetVofaRxCallback(Mspm0Runtime_UartRxCallback callback);
void Mspm0Runtime_SetVisionRxCallback(Mspm0Runtime_UartRxCallback callback);
void Mspm0Runtime_SetStepmotorTxCallback(Mspm0Runtime_UartTxCallback callback);

bool Mspm0Runtime_IsStepmotorTxBusy(void);

/**
 * @brief DMA TX send helpers for the three board UART roles.
 * @return true on start (or blocking byte fallback); false if busy/invalid.
 * @note  Copies into a private TX buffer before starting DMA. Returns false
 *        immediately when TX is busy (no unbounded wait).
 */
bool Mspm0Runtime_SendStepmotor(const uint8_t *data, uint32_t length);
bool Mspm0Runtime_SendVofa(const uint8_t *data, uint32_t length);

bool Mspm0Runtime_SendStepmotorByte(uint8_t data);

/**
 * @brief Read left/right encoder totals from the hardware QEI counters.
 * @param left  Optional; may be NULL.
 * @param right Optional; may be NULL.
 * @note  Widens the 16-bit QEI counters to int32_t internally; must be
 *        called at least once per 32767 counts of travel (always true for
 *        the periodic encoder task).
 */
void Mspm0Runtime_GetEncoderCounts(int32_t *left, int32_t *right);

/**
 * @brief Atomically read and clear key falling-edge bits set by GROUP1 IRQ.
 * @return Bit i corresponds to key K1+i. Consumed via BoardGpio.
 */
uint8_t Mspm0Runtime_ConsumeKeyIrqEdges(void);

#ifdef __cplusplus
}
#endif

#endif /* MSPM0_RUNTIME_H */
