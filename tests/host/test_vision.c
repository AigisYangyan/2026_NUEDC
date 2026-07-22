/**
 * @file    test_vision.c
 * @brief   视觉服务主机测试（V1，契约 §36）。
 *
 * 链接组成：真实 vision.c + uart_vision.c + uart_vofa.c + board_uart×4
 *           + fake_uart_port.c（视觉 RX 注入/TX 抓取 + VOFA TX 抓取）。
 * vision 取 now_ms 参数注入，不直读 Clock。
 */
#include "app/service/vision/vision.h"

#include "driver/board_uart/vision_uart.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_PushVisionBytes(const uint8_t *data, uint32_t length);
extern void FakeUartPort_CompleteVisionTx(void);
extern uint32_t FakeUartPort_CopyVisionTx(uint8_t *out, uint32_t capacity);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* CRC oracle（与 test_uart_vision 同款独立参照）。 */
static uint16_t ref_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t bit;

    for (i = 0u; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (bit = 0u; bit < 8u; bit++) {
            crc = ((crc & 1u) != 0u) ? (uint16_t)((crc >> 1) ^ 0xA001u)
                                     : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static uint32_t build_frame(uint8_t *buf, uint8_t cmd, const uint8_t *ext, uint8_t ext_len)
{
    uint8_t body[20];
    uint16_t crc;
    uint8_t len = (uint8_t)(1u + ext_len);
    uint8_t n = 0u;
    uint8_t i;

    buf[n++] = 0xAAu;
    buf[n++] = 0x55u;
    buf[n++] = len;
    buf[n++] = cmd;
    body[0] = len;
    body[1] = cmd;
    for (i = 0u; i < ext_len; i++) {
        buf[n++] = ext[i];
        body[2u + i] = ext[i];
    }
    crc = ref_crc16(body, (uint16_t)(2u + ext_len));
    buf[n++] = (uint8_t)(crc & 0xFFu);
    buf[n++] = (uint8_t)(crc >> 8);
    return n;
}

static void push_ack(uint8_t main_task, uint8_t sub_task)
{
    uint8_t ack[4];

    ack[0] = 0xFFu;
    ack[1] = main_task;
    ack[2] = sub_task;
    ack[3] = 0xFEu;
    FakeUartPort_PushVisionBytes(ack, sizeof(ack));
}

static void setup(void)
{
    FakeUartPort_ResetAll();
    (void)vofa_init();
    Vision_Init();
    Vision_Update(1000u);   /* 播种节拍基准 */
}

/* Init 零 TX。 */
static int test_init_zero_tx(void)
{
    uint8_t snap[8];

    setup();
    TEST_ASSERT_TRUE(FakeUartPort_CopyVisionTx(snap, sizeof(snap)) == 0u);
    printf("PASS: test_init_zero_tx\n");
    return 0;
}

/* SelectTopic 立即发 0xFF main sub 0xFE。 */
static int test_select_topic_sends_frame(void)
{
    uint8_t snap[8];

    setup();
    TEST_ASSERT_TRUE(Vision_SelectTopic(0x02u, 0x01u));
    TEST_ASSERT_TRUE(FakeUartPort_CopyVisionTx(snap, sizeof(snap)) == 4u);
    TEST_ASSERT_TRUE(snap[0] == 0xFFu && snap[1] == 0x02u && snap[2] == 0x01u && snap[3] == 0xFEu);
    printf("PASS: test_select_topic_sends_frame\n");
    return 0;
}

/* 未确认期 500ms 自动重发。 */
static int test_retry_at_500ms(void)
{
    setup();
    (void)Vision_SelectTopic(0x02u, 0x01u);
    FakeUartPort_CompleteVisionTx();
    (void)VisionUart_ConsumeTxDone();
    Vision_Update(1010u);
    TEST_ASSERT_TRUE(VisionUart_IsTxIdle());          /* <500ms 不重发 */
    Vision_Update(1490u);
    TEST_ASSERT_TRUE(VisionUart_IsTxIdle());
    Vision_Update(1500u);                             /* 距 select 500ms → 重发 */
    TEST_ASSERT_TRUE(!VisionUart_IsTxIdle());
    printf("PASS: test_retry_at_500ms\n");
    return 0;
}

/* 回显一致 → confirmed，且不再重发。 */
static int test_ack_match_confirms(void)
{
    setup();
    (void)Vision_SelectTopic(0x02u, 0x01u);
    FakeUartPort_CompleteVisionTx();
    (void)VisionUart_ConsumeTxDone();
    push_ack(0x02u, 0x01u);
    Vision_Update(1010u);
    TEST_ASSERT_TRUE(Vision_IsTopicConfirmed());
    Vision_Update(2000u);                             /* 远超 500ms：确认后零重发 */
    TEST_ASSERT_TRUE(VisionUart_IsTxIdle());
    printf("PASS: test_ack_match_confirms\n");
    return 0;
}

/* 回显不一致 → 不确认，继续重发。 */
static int test_ack_mismatch_keeps_retrying(void)
{
    setup();
    (void)Vision_SelectTopic(0x02u, 0x01u);
    FakeUartPort_CompleteVisionTx();
    (void)VisionUart_ConsumeTxDone();
    push_ack(0x09u, 0x09u);
    Vision_Update(1010u);
    TEST_ASSERT_TRUE(!Vision_IsTopicConfirmed());
    Vision_Update(1500u);
    TEST_ASSERT_TRUE(!VisionUart_IsTxIdle());         /* 重发发生 */
    printf("PASS: test_ack_mismatch_keeps_retrying\n");
    return 0;
}

/* 发起前的陈旧 ack 不算确认（seq 基线）。 */
static int test_stale_ack_ignored(void)
{
    setup();
    push_ack(0x02u, 0x01u);
    Vision_Update(1010u);                             /* 陈旧 ack 先入账 */
    (void)Vision_SelectTopic(0x02u, 0x01u);
    Vision_Update(1020u);
    Vision_Update(1030u);
    TEST_ASSERT_TRUE(!Vision_IsTopicConfirmed());
    printf("PASS: test_stale_ack_ignored\n");
    return 0;
}

/* 坐标/状态透传 + seq。 */
static int test_coord_status_passthrough(void)
{
    uint8_t frame[24];
    uint8_t ext_coord[8];
    uint8_t ext_st[2] = { 0x05u, 0x06u };
    uint8_t st[2] = { 0u, 0u };
    float x = 0.0f;
    float y = 0.0f;
    float fx = 123.5f;
    float fy = -7.25f;
    uint32_t n;

    setup();
    TEST_ASSERT_TRUE(!Vision_GetLatestCoord(&x, &y));
    memcpy(&ext_coord[0], &fx, 4u);
    memcpy(&ext_coord[4], &fy, 4u);
    n = build_frame(frame, 0x01u, ext_coord, 8u);
    FakeUartPort_PushVisionBytes(frame, n);
    n = build_frame(frame, 0x02u, ext_st, 2u);
    FakeUartPort_PushVisionBytes(frame, n);
    Vision_Update(1010u);
    TEST_ASSERT_TRUE(Vision_GetLatestCoord(&x, &y));
    TEST_ASSERT_TRUE(fabsf(x - fx) < 1e-6f && fabsf(y - fy) < 1e-6f);
    TEST_ASSERT_TRUE(Vision_CoordSeq() == 1u);
    TEST_ASSERT_TRUE(Vision_GetLatestStatus(st) && st[0] == 0x05u && st[1] == 0x06u);
    TEST_ASSERT_TRUE(Vision_StatusSeq() == 1u);
    printf("PASS: test_coord_status_passthrough\n");
    return 0;
}

/* 遥测 8 通道 + confirmed 镜像。 */
static int test_telemetry_channels(void)
{
    uint8_t raw[8u * 4u + 4u + 16u];
    float v[8];
    uint32_t n;
    uint32_t i;

    setup();
    Vision_StartTelemetry();
    (void)Vision_SelectTopic(0x02u, 0x01u);
    FakeUartPort_CompleteVisionTx();
    (void)VisionUart_ConsumeTxDone();
    push_ack(0x02u, 0x01u);
    Vision_Update(1010u);
    n = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    TEST_ASSERT_TRUE(n == (8u * 4u + 4u));            /* 8 通道 JustFloat = 36 字节 */
    for (i = 0u; i < 8u; i++) {
        memcpy(&v[i], &raw[i * 4u], 4u);
    }
    TEST_ASSERT_TRUE(v[6] == 1.0f);                   /* confirmed */
    Vision_StopTelemetry();
    printf("PASS: test_telemetry_channels\n");
    return 0;
}

/* 10ms 门控：未到期不 Poll。 */
static int test_gating_10ms(void)
{
    uint8_t frame[24];
    uint8_t ext_st[2] = { 0x01u, 0x02u };
    uint32_t n;

    setup();
    n = build_frame(frame, 0x02u, ext_st, 2u);
    FakeUartPort_PushVisionBytes(frame, n);
    Vision_Update(1005u);                             /* 5ms：未到期 */
    TEST_ASSERT_TRUE(Vision_StatusSeq() == 0u);
    Vision_Update(1010u);
    TEST_ASSERT_TRUE(Vision_StatusSeq() == 1u);
    printf("PASS: test_gating_10ms\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_zero_tx();
    failures += test_select_topic_sends_frame();
    failures += test_retry_at_500ms();
    failures += test_ack_match_confirms();
    failures += test_ack_mismatch_keeps_retrying();
    failures += test_stale_ack_ignored();
    failures += test_coord_status_passthrough();
    failures += test_telemetry_channels();
    failures += test_gating_10ms();

    if (failures != 0) {
        printf("%d vision service test(s) failed.\n", failures);
        return 1;
    }

    printf("All vision service tests passed.\n");
    return 0;
}
