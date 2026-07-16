/**
 * @file    clock.c
 * @brief   MSPM0G3507 SysTick-based millisecond clock implementation.
 *
 * Owns the SysTick configuration and the 1 ms counter.
 * Does NOT call any App layer functions from the ISR.
 */
#include "driver/clock/clock.h"

#include "ti_msp_dl_config.h"
#include <ti/driverlib/m0p/dl_systick.h>

#ifndef CPUCLK_FREQ
#error "CPUCLK_FREQ must be defined by ti_msp_dl_config.h"
#endif

#define CLOCK_SYSTICK_PERIOD_CYCLES (CPUCLK_FREQ / 1000u)

static volatile uint32_t s_tick_ms = 0u;

void Clock_Init(void)
{
    s_tick_ms = 0u;
    DL_SYSTICK_config(CLOCK_SYSTICK_PERIOD_CYCLES);
}

uint32_t Clock_NowMs(void)
{
    return s_tick_ms;
}

void SysTick_Handler(void)
{
    s_tick_ms++;
}
