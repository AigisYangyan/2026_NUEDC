#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/uart_vofa/uart_vofa.h"

void FakeUartPort_ResetAll(void);
void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length);
uint32_t Vofa_TestGetFrameParseCount(void);
uint32_t Vofa_TestGetRxDropCount(void);

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

static void test_segmented_frame_parses_once(void)
{
    volatile float kp = 0.0f;
    const uint8_t first_half[] = {'k', 'p', '=', '1'};
    const uint8_t second_half[] = {'.', '5', '\n'};

    FakeUartPort_ResetAll();
    (void)vofa_init();
    (void)vofa_bind_cmd("kp", &kp);

    FakeUartPort_PushVofaBytes(first_half, sizeof(first_half));
    vofa_run();
    expect_true((kp == 0.0f) && (Vofa_TestGetFrameParseCount() == 0u),
                "vofa segmented frame waits for delimiter");

    FakeUartPort_PushVofaBytes(second_half, sizeof(second_half));
    vofa_run();
    expect_true((kp > 1.49f) && (kp < 1.51f) &&
                    (Vofa_TestGetFrameParseCount() == 1u),
                "vofa segmented frame parses exactly once");
}

static void test_sticky_frames_parse_independently(void)
{
    volatile float p = 0.0f;
    volatile float d = 0.0f;
    const uint8_t payload[] = {'p', '=', '2', '.', '0', '\n',
                               'd', '=', '3', '.', '5', '\n'};

    FakeUartPort_ResetAll();
    (void)vofa_init();
    (void)vofa_bind_cmd("p", &p);
    (void)vofa_bind_cmd("d", &d);

    FakeUartPort_PushVofaBytes(payload, sizeof(payload));
    vofa_run();
    expect_true((p > 1.99f) && (p < 2.01f) &&
                    (d > 3.49f) && (d < 3.51f) &&
                    (Vofa_TestGetFrameParseCount() == 2u),
                "vofa sticky frames parse independently");
}

static void test_overlong_frame_is_dropped_without_overwrite(void)
{
    volatile float ki = 0.0f;
    uint8_t long_payload[80];
    const uint8_t valid_frame[] = {'k', 'i', '=', '7', '.', '0', '\n'};
    uint32_t index = 0u;

    for (index = 0u; index < sizeof(long_payload); index++) {
        long_payload[index] = (uint8_t)('A' + (index % 26u));
    }

    FakeUartPort_ResetAll();
    (void)vofa_init();
    (void)vofa_bind_cmd("ki", &ki);

    FakeUartPort_PushVofaBytes(long_payload, sizeof(long_payload));
    vofa_run();
    expect_true((ki == 0.0f) && (Vofa_TestGetRxDropCount() == 1u),
                "vofa overlong frame drops once without parse");

    FakeUartPort_PushVofaBytes(valid_frame, sizeof(valid_frame));
    vofa_run();
    expect_true((ki > 6.99f) && (ki < 7.01f) &&
                    (Vofa_TestGetFrameParseCount() == 1u),
                "vofa parser recovers after overlong frame");
}

static void test_delimiter_storm_produces_no_empty_parse(void)
{
    volatile float kd = 0.0f;
    const uint8_t payload[] = {'\n', '\r', ';', '\n', ';'};

    FakeUartPort_ResetAll();
    (void)vofa_init();
    (void)vofa_bind_cmd("kd", &kd);

    FakeUartPort_PushVofaBytes(payload, sizeof(payload));
    vofa_run();
    expect_true((kd == 0.0f) && (Vofa_TestGetFrameParseCount() == 0u),
                "vofa delimiter storm ignores empty frames");
}

int main(void)
{
    test_segmented_frame_parses_once();
    test_sticky_frames_parse_independently();
    test_overlong_frame_is_dropped_without_overwrite();
    test_delimiter_storm_produces_no_empty_parse();

    printf("All VOFA RX tests passed (%d tests).\n", s_test_count);
    return 0;
}
