/**
 * @file    fake_clock.c
 * @brief   Test-only fake for the Clock driver (Clock_NowMs settable).
 *
 * 仅链接进需要可控时间轴的服务级测试（test_chassis）；
 * test_encoder 等既有测试自带局部 Clock_NowMs 桩，不共用本文件。
 */
#include "driver/clock/clock.h"

static uint32_t s_now_ms;

void Clock_Init(void)
{
    s_now_ms = 0u;
}

uint32_t Clock_NowMs(void)
{
    return s_now_ms;
}

void FakeClock_Set(uint32_t now_ms)
{
    s_now_ms = now_ms;
}

void FakeClock_Advance(uint32_t delta_ms)
{
    s_now_ms += delta_ms;
}
