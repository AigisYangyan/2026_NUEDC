/**
 * @file    test_link.c
 * @brief   无线链路 Driver+Service 主机测试（WL1，契约 §35）。
 *
 * 链接组成：真实 uart_wireless.c + link.c + uart_vofa.c + board_uart×4
 *           + fake_wireless_port.c（无线端口）+ fake_uart_port.c（VOFA TX 抓取）。
 */
#include "app/service/link/link.h"
#include "driver/uart_wireless/uart_wireless.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void FakeWirelessPort_Reset(void);
extern void FakeWirelessPort_SetPresent(bool present);
extern void FakeWirelessPort_PushRx(const uint8_t *data, uint32_t len);
extern uint32_t FakeWirelessPort_CopyTx(uint8_t *out, uint32_t cap);
extern void FakeWirelessPort_ClearTx(void);
extern void FakeUartPort_ResetAll(void);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 测试侧 CRC 参照实现（协议 oracle，与被测实现独立）。 */
static uint16_t ref_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint8_t bit;

    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; bit++) {
            crc = ((crc & 1u) != 0u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/** 组一个合法帧到 out，返回总长。 */
static uint32_t build_frame(uint8_t *out, uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t body[2u + 32u];
    uint16_t crc;
    uint8_t i;

    out[0] = 0xA5u;
    out[1] = 0x5Au;
    out[2] = len;
    out[3] = type;
    body[0] = len;
    body[1] = type;
    for (i = 0u; i < len; i++) {
        out[4u + i] = payload[i];
        body[2u + i] = payload[i];
    }
    crc = ref_crc16(body, (uint32_t)len + 2u);
    out[4u + len] = (uint8_t)(crc & 0xFFu);
    out[5u + len] = (uint8_t)(crc >> 8);
    return (uint32_t)len + 6u;
}

static void setup_present(void)
{
    FakeWirelessPort_Reset();
    FakeUartPort_ResetAll();
    Link_Init();
    Link_Update(0u);        /* 播种周期/心跳基准 */
}

/* 心跳组帧字节序 + CRC（TX 精确断言）+ 200ms 节拍。 */
static int test_heartbeat_frame_and_cadence(void)
{
    uint8_t tx[64];
    uint8_t expect[8];
    uint32_t t;

    setup_present();
    Link_Update(10u);
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 0u);   /* <200ms 无心跳 */
    Link_Update(200u);
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 6u);
    (void)build_frame(expect, 0x02u, (const uint8_t *)"", 0u);
    TEST_ASSERT_TRUE(memcmp(tx, expect, 6u) == 0);

    for (t = 210u; t <= 1000u; t += 10u) {
        Link_Update(t);
    }
    /* 200/400/600/800/1000 → 共 5 帧 × 6 字节。 */
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 30u);
    printf("PASS: test_heartbeat_frame_and_cadence\n");
    return 0;
}

/* 脏前缀自同步 + 用户帧信箱。 */
static int test_selfsync_dirty_prefix(void)
{
    uint8_t frame[40];
    uint8_t dirty[3] = { 0x00u, 0xA5u, 0x77u };   /* 假头后跟非 5A：须回猎 */
    uint8_t payload[3] = { 0x11u, 0x22u, 0x33u };
    uint8_t buf[32];
    uint8_t len = 0u;
    uint32_t n;

    setup_present();
    FakeWirelessPort_PushRx(dirty, sizeof(dirty));
    n = build_frame(frame, 0x01u, payload, sizeof(payload));
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    TEST_ASSERT_TRUE(Link_TakeLatest(buf, sizeof(buf), &len));
    TEST_ASSERT_TRUE(len == 3u && memcmp(buf, payload, 3u) == 0);
    printf("PASS: test_selfsync_dirty_prefix\n");
    return 0;
}

/* 跨调用分片到达。 */
static int test_fragmented_arrival(void)
{
    uint8_t frame[40];
    uint8_t payload[2] = { 0xABu, 0xCDu };
    uint8_t buf[32];
    uint8_t len = 0u;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, payload, sizeof(payload));
    FakeWirelessPort_PushRx(frame, 3u);           /* 前 3 字节 */
    Link_Update(10u);
    TEST_ASSERT_TRUE(!Link_TakeLatest(buf, sizeof(buf), &len));
    FakeWirelessPort_PushRx(&frame[3], n - 3u);   /* 其余 */
    Link_Update(20u);
    TEST_ASSERT_TRUE(Link_TakeLatest(buf, sizeof(buf), &len) && len == 2u);
    printf("PASS: test_fragmented_arrival\n");
    return 0;
}

/* CRC 错拒收 + 计数；随后合法帧仍可收（自同步恢复）。 */
static int test_crc_reject_then_recover(void)
{
    uint8_t frame[40];
    uint8_t payload[1] = { 0x5Fu };
    uint8_t buf[32];
    uint8_t len = 0u;
    Link_Telemetry_T t;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, payload, 1u);
    frame[n - 1u] ^= 0xFFu;                       /* 毁 CRC 高字节 */
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Link_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.crc_errors == 1u && t.rx_frames == 0u);
    TEST_ASSERT_TRUE(!Link_TakeLatest(buf, sizeof(buf), &len));

    n = build_frame(frame, 0x01u, payload, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(20u);
    TEST_ASSERT_TRUE(Link_TakeLatest(buf, sizeof(buf), &len) && buf[0] == 0x5Fu);
    printf("PASS: test_crc_reject_then_recover\n");
    return 0;
}

/* len 越界（>32）拒收计数 + 恢复。 */
static int test_len_overflow_reject(void)
{
    uint8_t bad[4] = { 0xA5u, 0x5Au, 33u, 0x01u };
    uint8_t frame[40];
    uint8_t payload[1] = { 0x66u };
    uint8_t buf[32];
    uint8_t len = 0u;
    Link_Telemetry_T t;
    uint32_t n;

    setup_present();
    FakeWirelessPort_PushRx(bad, sizeof(bad));
    n = build_frame(frame, 0x01u, payload, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Link_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.crc_errors == 1u);
    TEST_ASSERT_TRUE(Link_TakeLatest(buf, sizeof(buf), &len) && buf[0] == 0x66u);
    printf("PASS: test_len_overflow_reject\n");
    return 0;
}

/* 信箱后到覆盖 + 一次性消费 + cap 不足保留。 */
static int test_mailbox_semantics(void)
{
    uint8_t frame[40];
    uint8_t p1[2] = { 0x01u, 0x02u };
    uint8_t p2[4] = { 0x0Au, 0x0Bu, 0x0Cu, 0x0Du };
    uint8_t buf[32];
    uint8_t len = 0u;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, p1, 2u);
    FakeWirelessPort_PushRx(frame, n);
    n = build_frame(frame, 0x01u, p2, 4u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    TEST_ASSERT_TRUE(!Link_TakeLatest(buf, 2u, &len));            /* cap 不足 → 保留 */
    TEST_ASSERT_TRUE(Link_TakeLatest(buf, sizeof(buf), &len));    /* 后到覆盖先到 */
    TEST_ASSERT_TRUE(len == 4u && memcmp(buf, p2, 4u) == 0);
    TEST_ASSERT_TRUE(!Link_TakeLatest(buf, sizeof(buf), &len));   /* 一次性消费 */
    printf("PASS: test_mailbox_semantics\n");
    return 0;
}

/* 超长 payload 拒发。 */
static int test_send_too_long_rejected(void)
{
    uint8_t big[33];

    setup_present();
    memset(big, 0xEEu, sizeof(big));
    TEST_ASSERT_TRUE(!Link_Send(big, 33u));
    TEST_ASSERT_TRUE(Link_Send(big, 32u));
    printf("PASS: test_send_too_long_rejected\n");
    return 0;
}

/* 活性窗口：收帧→alive，600ms 静默→dead；未收过帧恒 false。 */
static int test_alive_window(void)
{
    uint8_t frame[40];
    uint32_t n;

    setup_present();
    Link_Update(10u);
    TEST_ASSERT_TRUE(!Link_IsAlive());            /* 从未收到帧 */
    n = build_frame(frame, 0x02u, (const uint8_t *)"", 0u);   /* 对端心跳 */
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(20u);
    TEST_ASSERT_TRUE(Link_IsAlive());
    Link_Update(620u);                            /* 距上帧 600ms 整：仍在窗口 */
    TEST_ASSERT_TRUE(Link_IsAlive());
    Link_Update(630u);                            /* 610ms：出窗 */
    TEST_ASSERT_TRUE(!Link_IsAlive());
    printf("PASS: test_alive_window\n");
    return 0;
}

/* 端口缺席如实上报：Send false、hb_sent 恒 0、alive 恒 false、不崩。 */
static int test_port_absent_honest(void)
{
    uint8_t d[1] = { 0x77u };
    Link_Telemetry_T t;
    uint32_t i;

    FakeWirelessPort_Reset();
    FakeWirelessPort_SetPresent(false);
    FakeUartPort_ResetAll();
    Link_Init();
    for (i = 0u; i <= 100u; i++) {
        Link_Update(i * 10u);
    }
    Link_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.port_absent && t.hb_sent == 0u && !t.alive);
    TEST_ASSERT_TRUE(!Link_Send(d, 1u));
    printf("PASS: test_port_absent_honest\n");
    return 0;
}

/* 10ms 门控：不足周期不泵（心跳不会因高频调用而超发）。 */
static int test_gating_10ms(void)
{
    uint8_t tx[64];
    uint32_t i;

    setup_present();
    for (i = 0u; i < 100u; i++) {
        Link_Update(5u);            /* 距播种 5ms，恒未到期 */
    }
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 0u);
    printf("PASS: test_gating_10ms\n");
    return 0;
}

/* LinkTest 遥测：6 通道帧 + 值镜像一致。 */
static int test_telemetry_channels(void)
{
    uint8_t frame[40];
    uint8_t raw[6u * 4u + 4u + 16u];
    int ch5_port_absent;
    uint32_t n;

    FakeWirelessPort_Reset();
    FakeUartPort_ResetAll();
    (void)vofa_init();
    Link_Init();
    Link_StartTelemetry();
    Link_Update(0u);
    n = build_frame(frame, 0x02u, (const uint8_t *)"", 0u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    n = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    TEST_ASSERT_TRUE(n == (6u * 4u + 4u));        /* 6 通道 JustFloat = 28 字节 */
    {
        float v[6];
        uint32_t i;
        for (i = 0u; i < 6u; i++) {
            memcpy(&v[i], &raw[i * 4u], 4u);
        }
        TEST_ASSERT_TRUE(v[0] == 1.0f);           /* alive */
        TEST_ASSERT_TRUE(v[1] == 1.0f);           /* rx_frames */
        ch5_port_absent = (int)v[5];
        TEST_ASSERT_TRUE(ch5_port_absent == 0);
    }
    Link_StopTelemetry();
    printf("PASS: test_telemetry_channels\n");
    return 0;
}

/* 用户帧 TX 组帧正确（回环 oracle：把自己发的帧喂回解析器能收到）。 */
static int test_send_user_loopback(void)
{
    uint8_t payload[5] = { 1u, 2u, 3u, 4u, 5u };
    uint8_t tx[64];
    uint8_t buf[32];
    uint8_t len = 0u;
    uint32_t n;

    setup_present();
    TEST_ASSERT_TRUE(Link_Send(payload, 5u));
    n = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    TEST_ASSERT_TRUE(n == 11u);                   /* 6 + 5 */
    FakeWirelessPort_PushRx(tx, n);               /* 自环回（杜邦短接的主机版） */
    Link_Update(10u);
    TEST_ASSERT_TRUE(Link_TakeLatest(buf, sizeof(buf), &len));
    TEST_ASSERT_TRUE(len == 5u && memcmp(buf, payload, 5u) == 0);
    TEST_ASSERT_TRUE(Link_IsAlive());             /* 自环回即活性=字节层 OK 的验收剧本 */
    printf("PASS: test_send_user_loopback\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_heartbeat_frame_and_cadence();
    failures += test_selfsync_dirty_prefix();
    failures += test_fragmented_arrival();
    failures += test_crc_reject_then_recover();
    failures += test_len_overflow_reject();
    failures += test_mailbox_semantics();
    failures += test_send_too_long_rejected();
    failures += test_alive_window();
    failures += test_port_absent_honest();
    failures += test_gating_10ms();
    failures += test_telemetry_channels();
    failures += test_send_user_loopback();

    if (failures != 0) {
        printf("%d link test(s) failed.\n", failures);
        return 1;
    }

    printf("All wireless link tests passed.\n");
    return 0;
}
