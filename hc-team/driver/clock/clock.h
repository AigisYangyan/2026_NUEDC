/**
 * @file    clock.h
 * @brief   Monotonic millisecond clock Driver.
 *
 * Provides a single, hardware-backed 1 ms tick source.
 * The SysTick ISR only increments the counter; all consumers must actively
 * query Clock_NowMs() and compute elapsed time with unsigned subtraction.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the 1 ms SysTick and reset the millisecond counter.
 * @note Must be called exactly once, before interrupts are enabled.
 */
void Clock_Init(void);

/**
 * @brief Return the current monotonic millisecond counter.
 * @return uint32_t tick count that is allowed to wrap.
 * @note Compute elapsed time as `uint32_t elapsed = Clock_NowMs() - start;` — the
 *       unsigned subtraction naturally wraps modulo 2^32 with no extra operator.
 */
uint32_t Clock_NowMs(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_H */
