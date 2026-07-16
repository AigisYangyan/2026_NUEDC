/**
 * @file    board_gpio.h
 * @brief   Board-level GPIO IRQ snapshot interface.
 *
 * Provides a consistent, atomic snapshot of the raw encoder totals
 * maintained by the shared GPIO IRQ handler. This is the boundary
 * between the interrupt-driven quadrature counting and the Encoder Driver.
 *
 * @note This module is intentionally minimal for P2.2/P4.1 transitional
 *       shared GPIO ownership. Encoder totals still come from Runtime, while
 *       key edges and raw pressed levels are exposed as pull-based snapshots.
 */
#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Raw encoder totals captured in one short critical section. */
typedef struct {
    int32_t left;
    int32_t right;
} BoardEncoderRawSnapshot;

/**
 * @brief Read a consistent snapshot of left/right raw encoder totals.
 * @param out  Caller-owned output structure; must be non-NULL.
 * @return true if the snapshot was filled; false if out is NULL.
 */
bool BoardGpio_GetEncoderRawSnapshot(BoardEncoderRawSnapshot *out);

/**
 * @brief Atomically read and clear pending key falling-edge bits.
 * @return Bit i corresponds to KEY_ID_K1 + i.
 */
uint8_t BoardGpio_ConsumeKeyIrqEdges(void);

/**
 * @brief Read current key raw pressed levels in one short critical section.
 * @return Bit i corresponds to KEY_ID_K1 + i; bit=1 means currently pressed.
 */
uint8_t BoardGpio_GetKeyRawLevels(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_GPIO_H */
