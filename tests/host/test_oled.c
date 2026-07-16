#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/oled/oled_hardware_i2c.h"

void FakeI2cPort_Reset(void);
void FakeI2cPort_SetNowMs(uint32_t now_ms);
void FakeI2cPort_SetBusyStuck(bool enabled);
void FakeI2cPort_SetTxFifoStuck(bool enabled);
void FakeI2cPort_SetControllerError(bool enabled);
uint32_t FakeI2cPort_GetWriteCount(void);
uint32_t FakeI2cPort_GetTransferCount(void);

static int s_test_count = 0;

static void expect_true(bool condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }

    s_test_count++;
    printf("PASS: %s\n", name);
}

static void advance_init_to_ready(void)
{
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    FakeI2cPort_SetNowMs(200u);
    expect_true(OLED_Process() == OLED_OK, "oled init process succeeds");
    expect_true(OLED_IsReady() == true, "oled ready after init succeeds");
}

static void test_ready_is_false_before_init(void)
{
    FakeI2cPort_Reset();
    expect_true(OLED_IsReady() == false, "oled ready false before init");
}

static void test_init_timeout_reports_error_and_keeps_unready(void)
{
    FakeI2cPort_Reset();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    FakeI2cPort_SetNowMs(200u);
    FakeI2cPort_SetBusyStuck(true);

    expect_true(OLED_Process() == OLED_ERR_TIMEOUT,
                "oled init timeout returns timeout");
    expect_true(OLED_IsReady() == false,
                "oled stays unready after init timeout");

    FakeI2cPort_SetBusyStuck(false);
    expect_true(OLED_Process() == OLED_OK,
                "oled init recovers after timeout is removed");
    expect_true(OLED_IsReady() == true,
                "oled becomes ready after retry succeeds");
}

static void test_runtime_error_is_reported_without_clearing_ready(void)
{
    FakeI2cPort_Reset();
    advance_init_to_ready();
    FakeI2cPort_SetBusyStuck(true);

    expect_true(OLED_Clear() == OLED_ERR_TIMEOUT,
                "oled clear reports runtime timeout");
    expect_true(OLED_IsReady() == true,
                "oled ready remains true after runtime timeout");
}

static void test_show_char_out_of_bounds_rejects_without_writes(void)
{
    uint32_t writes_before = 0u;

    FakeI2cPort_Reset();
    advance_init_to_ready();
    writes_before = FakeI2cPort_GetWriteCount();

    expect_true(OLED_ShowChar(127u, 7u, 'A', 16u) == OLED_ERR_INVALID,
                "oled show char rejects out of bounds");
    expect_true(FakeI2cPort_GetWriteCount() == writes_before,
                "oled show char out of bounds writes nothing");
}

static void test_init_waits_for_power_stable_delay(void)
{
    FakeI2cPort_Reset();
    FakeI2cPort_SetNowMs(0u);
    OLED_Init();
    FakeI2cPort_SetNowMs(199u);

    expect_true(OLED_Process() == OLED_OK,
                "oled process before delay returns ok");
    expect_true((OLED_IsReady() == false) &&
                    (FakeI2cPort_GetTransferCount() == 0u),
                "oled process before delay does not start transfer");
}

int main(void)
{
    test_ready_is_false_before_init();
    test_init_waits_for_power_stable_delay();
    test_init_timeout_reports_error_and_keeps_unready();
    test_runtime_error_is_reported_without_clearing_ready();
    test_show_char_out_of_bounds_rejects_without_writes();

    printf("All OLED tests passed (%d tests).\n", s_test_count);
    return 0;
}
