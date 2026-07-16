/**
 * @file    mspm0_runtime.h
 * @brief   Board-specific MSPM0G3519 runtime boundary for UART/DMA IRQ entry,
 *          shared GPIO IRQ state, and QEI readout.
 *
 * This is not a reusable HAL. Hardware details stay in mspm0_runtime.c.
 * Public APIs use only standard C types.
 */
#ifndef MSPM0_RUNTIME_H
#define MSPM0_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UART/DMA IRQ dispatch used by the fixed board UART roles.
 * @note  Owns DMA/UART IRQ entry, RX rearm, and role-level byte dispatch.
 */
void Mspm0Runtime_InitUartDma(void);

void Mspm0Runtime_DelayMs(uint32_t delay_ms);

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
