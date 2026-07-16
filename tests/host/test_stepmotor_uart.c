#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/board_uart/stepmotor_uart.h"

void FakeUartPort_ResetAll(void);
void FakeUartPort_CompleteStepmotorTx(void);
uint32_t FakeUartPort_CopyStepmotorTx(uint8_t *out, uint32_t capacity);

#define HOST_STEPMOTOR_UART_MAX_TX 32u

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

static void fill_pattern(uint8_t *buffer, uint32_t length, uint8_t seed)
{
    uint32_t index = 0u;

    for (index = 0u; index < length; index++) {
        buffer[index] = (uint8_t)(seed + index);
    }
}

static void test_max_length_send_succeeds(void)
{
    uint8_t frame[HOST_STEPMOTOR_UART_MAX_TX];
    uint8_t snapshot[HOST_STEPMOTOR_UART_MAX_TX];

    fill_pattern(frame, sizeof(frame), 0x10u);
    FakeUartPort_ResetAll();

    expect_true(StepmotorUart_TryWrite(frame, sizeof(frame)) == true,
                "stepmotor max length send succeeds");
    expect_true((StepmotorUart_IsTxIdle() == false) &&
                    (FakeUartPort_CopyStepmotorTx(snapshot, sizeof(snapshot)) ==
                     sizeof(frame)) &&
                    (memcmp(snapshot, frame, sizeof(frame)) == 0),
                "stepmotor send copies payload into private buffer");
}

static void test_busy_rejects_without_overwrite(void)
{
    uint8_t first_frame[HOST_STEPMOTOR_UART_MAX_TX];
    uint8_t second_frame[HOST_STEPMOTOR_UART_MAX_TX];
    uint8_t snapshot[HOST_STEPMOTOR_UART_MAX_TX];

    fill_pattern(first_frame, sizeof(first_frame), 0x30u);
    fill_pattern(second_frame, sizeof(second_frame), 0x70u);
    FakeUartPort_ResetAll();

    expect_true(StepmotorUart_TryWrite(first_frame, sizeof(first_frame)) == true,
                "stepmotor first frame arms tx");
    expect_true(StepmotorUart_TryWrite(second_frame, sizeof(second_frame)) == false,
                "stepmotor busy rejects second frame");
    expect_true((FakeUartPort_CopyStepmotorTx(snapshot, sizeof(snapshot)) ==
                 sizeof(first_frame)) &&
                    (memcmp(snapshot, first_frame, sizeof(first_frame)) == 0),
                "stepmotor busy reject keeps previous private buffer");
}

static void test_oversized_send_is_rejected(void)
{
    uint8_t frame[HOST_STEPMOTOR_UART_MAX_TX + 1u];

    fill_pattern(frame, sizeof(frame), 0x90u);
    FakeUartPort_ResetAll();

    expect_true(StepmotorUart_TryWrite(frame, sizeof(frame)) == false,
                "stepmotor oversized frame is rejected");
    expect_true(StepmotorUart_IsTxIdle() == true,
                "stepmotor oversized frame leaves tx idle");
}

static void test_tx_done_consumes_once(void)
{
    uint8_t frame[8];

    fill_pattern(frame, sizeof(frame), 0xB0u);
    FakeUartPort_ResetAll();

    expect_true(StepmotorUart_TryWrite(frame, sizeof(frame)) == true,
                "stepmotor short frame arms tx");
    FakeUartPort_CompleteStepmotorTx();
    expect_true(StepmotorUart_ConsumeTxDone() == true,
                "stepmotor tx done is observed once");
    expect_true((StepmotorUart_ConsumeTxDone() == false) &&
                    (StepmotorUart_IsTxIdle() == true),
                "stepmotor tx done second consume is false");
}

int main(void)
{
    test_max_length_send_succeeds();
    test_busy_rejects_without_overwrite();
    test_oversized_send_is_rejected();
    test_tx_done_consumes_once();

    printf("All stepmotor UART tests passed (%d tests).\n", s_test_count);
    return 0;
}
