#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/board_uart/stepmotor_uart.h"
#include "driver/board_uart/vision_uart.h"
#include "driver/board_uart/vofa_uart.h"

void FakeUartPort_ResetAll(void);
void FakeUartPort_PushVisionBytes(const uint8_t *data, uint32_t length);
void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length);
void FakeUartPort_PushStepmotorBytes(const uint8_t *data, uint32_t length);
void FakeUartPort_CompleteVofaTx(void);
uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

#define VISION_FIFO_CAPACITY 512u
#define VOFA_FIFO_CAPACITY 512u
#define STEPMOTOR_FIFO_CAPACITY 256u
#define VOFA_TX_RING_CAPACITY 512u

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

static void test_vision_fifo_suite(void)
{
    uint8_t read_buf[VISION_FIFO_CAPACITY];
    uint8_t payload[VISION_FIFO_CAPACITY + 8u];
    uint8_t small_buf[9];
    uint32_t read_len = 0u;

    FakeUartPort_ResetAll();
    expect_true(VisionUart_Read(read_buf, sizeof(read_buf)) == 0u,
                "vision empty read");

    FakeUartPort_ResetAll();
    payload[0] = 0x11u;
    FakeUartPort_PushVisionBytes(payload, 1u);
    read_len = VisionUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == 1u) && (read_buf[0] == 0x11u),
                "vision single byte");

    FakeUartPort_ResetAll();
    fill_pattern(payload, 32u, 0x20u);
    FakeUartPort_PushVisionBytes(payload, 32u);
    read_len = VisionUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == 32u) && (memcmp(read_buf, payload, 32u) == 0),
                "vision bulk read");

    FakeUartPort_ResetAll();
    fill_pattern(payload, VISION_FIFO_CAPACITY, 0x40u);
    FakeUartPort_PushVisionBytes(payload, VISION_FIFO_CAPACITY);
    read_len = VisionUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == VISION_FIFO_CAPACITY) &&
                    (memcmp(read_buf, payload, VISION_FIFO_CAPACITY) == 0),
                "vision exact full capacity");

    FakeUartPort_ResetAll();
    fill_pattern(payload, VISION_FIFO_CAPACITY + 4u, 0x60u);
    FakeUartPort_PushVisionBytes(payload, VISION_FIFO_CAPACITY + 4u);
    read_len = VisionUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == VISION_FIFO_CAPACITY) &&
                    (memcmp(read_buf, payload, VISION_FIFO_CAPACITY) == 0) &&
                    (VisionUart_GetRxOverflowCount() == 4u),
                "vision overflow keeps old bytes and increments counter");

    FakeUartPort_ResetAll();
    fill_pattern(payload, VISION_FIFO_CAPACITY, 0x80u);
    FakeUartPort_PushVisionBytes(payload, VISION_FIFO_CAPACITY - 3u);
    read_len = VisionUart_Read(read_buf, 5u);
    expect_true((read_len == 5u) && (memcmp(read_buf, payload, 5u) == 0),
                "vision read truncates to capacity");
    FakeUartPort_PushVisionBytes(&payload[VISION_FIFO_CAPACITY - 3u], 3u);
    read_len = VisionUart_Read(small_buf, sizeof(small_buf));
    expect_true((read_len == 9u) &&
                    (memcmp(small_buf, &payload[5u], 9u) == 0),
                "vision wrap after partial read");
}

static void test_vofa_fifo_suite(void)
{
    uint8_t read_buf[VOFA_FIFO_CAPACITY];
    uint8_t payload[VOFA_FIFO_CAPACITY + 8u];
    uint32_t read_len = 0u;

    FakeUartPort_ResetAll();
    fill_pattern(payload, 19u, 0x10u);
    FakeUartPort_PushVofaBytes(payload, 19u);
    read_len = VofaUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == 19u) && (memcmp(read_buf, payload, 19u) == 0),
                "vofa bulk read");

    FakeUartPort_ResetAll();
    fill_pattern(payload, VOFA_FIFO_CAPACITY + 2u, 0x22u);
    FakeUartPort_PushVofaBytes(payload, VOFA_FIFO_CAPACITY + 2u);
    read_len = VofaUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == VOFA_FIFO_CAPACITY) &&
                    (memcmp(read_buf, payload, VOFA_FIFO_CAPACITY) == 0) &&
                    (VofaUart_GetRxOverflowCount() == 2u),
                "vofa overflow keeps old bytes and increments counter");
}

static void test_stepmotor_fifo_suite(void)
{
    uint8_t read_buf[STEPMOTOR_FIFO_CAPACITY];
    uint8_t payload[STEPMOTOR_FIFO_CAPACITY + 8u];
    uint32_t read_len = 0u;

    FakeUartPort_ResetAll();
    fill_pattern(payload, STEPMOTOR_FIFO_CAPACITY - 1u, 0x31u);
    FakeUartPort_PushStepmotorBytes(payload, STEPMOTOR_FIFO_CAPACITY - 1u);
    read_len = StepmotorUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == STEPMOTOR_FIFO_CAPACITY - 1u) &&
                    (memcmp(read_buf, payload, STEPMOTOR_FIFO_CAPACITY - 1u) == 0),
                "stepmotor bulk read");

    FakeUartPort_ResetAll();
    fill_pattern(payload, STEPMOTOR_FIFO_CAPACITY + 3u, 0x51u);
    FakeUartPort_PushStepmotorBytes(payload, STEPMOTOR_FIFO_CAPACITY + 3u);
    read_len = StepmotorUart_Read(read_buf, sizeof(read_buf));
    expect_true((read_len == STEPMOTOR_FIFO_CAPACITY) &&
                    (memcmp(read_buf, payload, STEPMOTOR_FIFO_CAPACITY) == 0) &&
                    (StepmotorUart_GetRxOverflowCount() == 3u),
                "stepmotor overflow keeps old bytes and increments counter");

    FakeUartPort_ResetAll();
    fill_pattern(payload, 40u, 0x71u);
    FakeUartPort_PushStepmotorBytes(payload, 17u);
    read_len = StepmotorUart_Read(read_buf, 9u);
    expect_true((read_len == 9u) && (memcmp(read_buf, payload, 9u) == 0),
                "stepmotor read truncates to capacity");
    FakeUartPort_PushStepmotorBytes(&payload[17u], 23u);
    read_len = StepmotorUart_Read(read_buf, 31u);
    expect_true((read_len == 31u) &&
                    (memcmp(read_buf, &payload[9u], 31u) == 0),
                "stepmotor wrap after partial read");
}

/* VOFA 传输层 TX 软件队列（T-VTXQ1）：忙时入队而非丢帧，完成中断排空下一段。
 * VOFA JustFloat/FireWater 是按尾标重同步的字节流，跨 DMA 段合并搬运合法，
 * 故断言以字节流按序为准，不要求逐帧再分段。 */
static void test_vofa_tx_queue_suite(void)
{
    uint8_t frame_a[3] = {0xA1u, 0xA2u, 0xA3u};
    uint8_t frame_b[2] = {0xB1u, 0xB2u};
    uint8_t frame_c[2] = {0xC1u, 0xC2u};
    uint8_t seen[16];
    uint32_t seen_len = 0u;

    /* 忙时入队：A 在途未完成时提交 B —— 旧码丢帧(false)，新码入队(true) */
    FakeUartPort_ResetAll();
    expect_true(VofaUart_TryWrite(frame_a, 3u) == true,
                "vofa tx first frame accepted");
    seen_len = FakeUartPort_CopyVofaTx(seen, sizeof(seen));
    expect_true((seen_len == 3u) && (memcmp(seen, frame_a, 3u) == 0),
                "vofa tx first frame is in flight");
    expect_true(VofaUart_TryWrite(frame_b, 2u) == true,
                "vofa tx queue holds frame while busy");
    seen_len = FakeUartPort_CopyVofaTx(seen, sizeof(seen));
    expect_true((seen_len == 3u) && (memcmp(seen, frame_a, 3u) == 0),
                "vofa tx in-flight frame not preempted by queued frame");
    FakeUartPort_CompleteVofaTx();
    seen_len = FakeUartPort_CopyVofaTx(seen, sizeof(seen));
    expect_true((seen_len == 2u) && (memcmp(seen, frame_b, 2u) == 0),
                "vofa tx queue drains next frame on completion");

    /* 排空后回空闲，再次提交立即在途 */
    FakeUartPort_CompleteVofaTx();
    expect_true(VofaUart_TryWrite(frame_c, 2u) == true,
                "vofa tx accepts after drain");
    seen_len = FakeUartPort_CopyVofaTx(seen, sizeof(seen));
    expect_true((seen_len == 2u) && (memcmp(seen, frame_c, 2u) == 0),
                "vofa tx re-kicks when idle");

    /* 背靠背多帧入队：完成后按字节序合并搬运（B||C） */
    FakeUartPort_ResetAll();
    expect_true(VofaUart_TryWrite(frame_a, 3u) == true,
                "vofa tx burst first accepted");
    expect_true(VofaUart_TryWrite(frame_b, 2u) == true,
                "vofa tx burst second queued");
    expect_true(VofaUart_TryWrite(frame_c, 2u) == true,
                "vofa tx burst third queued");
    FakeUartPort_CompleteVofaTx();
    seen_len = FakeUartPort_CopyVofaTx(seen, sizeof(seen));
    expect_true((seen_len == 4u) && (seen[0] == 0xB1u) && (seen[1] == 0xB2u) &&
                    (seen[2] == 0xC1u) && (seen[3] == 0xC2u),
                "vofa tx queue preserves byte order across frames");

    /* 队列满则拒绝并计数（有界背压，不静默丢） */
    {
        uint8_t big[VOFA_TX_RING_CAPACITY];

        FakeUartPort_ResetAll();
        fill_pattern(big, sizeof(big), 0x10u);
        expect_true(VofaUart_TryWrite(frame_a, 1u) == true,
                    "vofa tx overflow setup keeps one frame in flight");
        expect_true(VofaUart_TryWrite(big, VOFA_TX_RING_CAPACITY) == true,
                    "vofa tx fills ring to capacity");
        expect_true(VofaUart_TryWrite(frame_b, 1u) == false,
                    "vofa tx overflow rejects and counts");
        expect_true(VofaUart_GetTxOverflowCount() == 1u,
                    "vofa tx overflow increments counter");
    }
}

int main(void)
{
    test_vision_fifo_suite();
    test_vofa_fifo_suite();
    test_stepmotor_fifo_suite();
    test_vofa_tx_queue_suite();

    printf("All UART FIFO tests passed (%d tests).\n", s_test_count);
    return 0;
}
