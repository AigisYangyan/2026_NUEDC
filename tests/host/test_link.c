/**
 * @file    test_link.c
 * @brief   无线链路 Driver+Service 主机测试（WL2 协议 v2，契约 §38）。
 *
 * 链接组成：真实 uart_wireless.c + link.c + uart_vofa.c + board_uart×4
 *           + fake_wireless_port.c（无线端口）+ fake_uart_port.c（VOFA TX 抓取）。
 *
 * 帧 v2：0xA5 0x5A | len | type | seq | payload | CRC16-MODBUS LE（范围含 seq）。
 * type：0x01=STATE（不可靠，最新胜出）0x02=心跳（与 STATE 共 seq）
 *       0x03=EVENT（必达）0x04=ACK（seq=被确认事件 seq）。
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

/** 组一个合法 v2 帧到 out，返回总长（len+7）。 */
static uint32_t build_frame(uint8_t *out, uint8_t type, uint8_t seq,
                            const uint8_t *payload, uint8_t len)
{
    uint8_t body[3u + 32u];
    uint16_t crc;
    uint8_t i;

    out[0] = 0xA5u;
    out[1] = 0x5Au;
    out[2] = len;
    out[3] = type;
    out[4] = seq;
    body[0] = len;
    body[1] = type;
    body[2] = seq;
    for (i = 0u; i < len; i++) {
        out[5u + i] = payload[i];
        body[3u + i] = payload[i];
    }
    crc = ref_crc16(body, (uint32_t)len + 3u);
    out[5u + len] = (uint8_t)(crc & 0xFFu);
    out[6u + len] = (uint8_t)(crc >> 8);
    return (uint32_t)len + 7u;
}

static void setup_present(void)
{
    FakeWirelessPort_Reset();
    FakeUartPort_ResetAll();
    Link_Init();
    Link_Update(0u);        /* 播种周期/心跳基准 */
}

/* 心跳组帧字节序（含 seq 递增）+ CRC + 200ms 节拍。 */
static int test_heartbeat_frame_and_cadence(void)
{
    uint8_t tx[64];
    uint8_t expect[8];
    uint32_t t;

    setup_present();
    Link_Update(10u);
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 0u);   /* <200ms 无心跳 */
    Link_Update(200u);
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 7u);
    (void)build_frame(expect, 0x02u, 0u, (const uint8_t *)"", 0u);
    TEST_ASSERT_TRUE(memcmp(tx, expect, 7u) == 0);

    for (t = 210u; t <= 1000u; t += 10u) {
        Link_Update(t);
    }
    /* 200/400/600/800/1000 → 共 5 帧 × 7 字节，seq 0..4 单调。 */
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 35u);
    TEST_ASSERT_TRUE(tx[4] == 0u && tx[11] == 1u && tx[32] == 4u);
    printf("PASS: test_heartbeat_frame_and_cadence\n");
    return 0;
}

/* 脏前缀自同步 + 状态帧信箱。 */
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
    n = build_frame(frame, 0x01u, 0u, payload, sizeof(payload));
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len));
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
    n = build_frame(frame, 0x01u, 0u, payload, sizeof(payload));
    FakeWirelessPort_PushRx(frame, 3u);           /* 前 3 字节 */
    Link_Update(10u);
    TEST_ASSERT_TRUE(!Link_TakeState(buf, sizeof(buf), &len));
    FakeWirelessPort_PushRx(&frame[3], n - 3u);   /* 其余 */
    Link_Update(20u);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len) && len == 2u);
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
    n = build_frame(frame, 0x01u, 0u, payload, 1u);
    frame[n - 1u] ^= 0xFFu;                       /* 毁 CRC 高字节 */
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Link_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.crc_errors == 1u && t.rx_frames == 0u);
    TEST_ASSERT_TRUE(!Link_TakeState(buf, sizeof(buf), &len));

    n = build_frame(frame, 0x01u, 1u, payload, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(20u);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len) && buf[0] == 0x5Fu);
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
    n = build_frame(frame, 0x01u, 0u, payload, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Link_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.crc_errors == 1u);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len) && buf[0] == 0x66u);
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
    n = build_frame(frame, 0x01u, 0u, p1, 2u);
    FakeWirelessPort_PushRx(frame, n);
    n = build_frame(frame, 0x01u, 1u, p2, 4u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    TEST_ASSERT_TRUE(!Link_TakeState(buf, 2u, &len));            /* cap 不足 → 保留 */
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len));    /* 后到覆盖先到 */
    TEST_ASSERT_TRUE(len == 4u && memcmp(buf, p2, 4u) == 0);
    TEST_ASSERT_TRUE(!Link_TakeState(buf, sizeof(buf), &len));   /* 一次性消费 */
    printf("PASS: test_mailbox_semantics\n");
    return 0;
}

/* 超长 payload 拒发。 */
static int test_send_too_long_rejected(void)
{
    uint8_t big[33];

    setup_present();
    memset(big, 0xEEu, sizeof(big));
    TEST_ASSERT_TRUE(!Link_SendState(big, 33u));
    TEST_ASSERT_TRUE(Link_SendState(big, 32u));
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
    n = build_frame(frame, 0x02u, 0u, (const uint8_t *)"", 0u);   /* 对端心跳 */
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

/* 端口缺席如实上报：SendState/SendEvent false、hb_sent 恒 0、alive 恒 false、不崩。 */
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
    TEST_ASSERT_TRUE(!Link_SendState(d, 1u));
    TEST_ASSERT_TRUE(!Link_SendEvent(d, 1u));
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

/* LinkTest 遥测：10 通道帧 + 值镜像一致。 */
static int test_telemetry_channels(void)
{
    uint8_t frame[40];
    uint8_t raw[10u * 4u + 4u + 16u];
    uint32_t n;

    FakeWirelessPort_Reset();
    FakeUartPort_ResetAll();
    (void)vofa_init();
    Link_Init();
    Link_StartTelemetry();
    Link_Update(0u);
    n = build_frame(frame, 0x02u, 0u, (const uint8_t *)"", 0u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    n = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    TEST_ASSERT_TRUE(n == (10u * 4u + 4u));       /* 10 通道 JustFloat = 44 字节 */
    {
        float v[10];
        uint32_t i;
        for (i = 0u; i < 10u; i++) {
            memcpy(&v[i], &raw[i * 4u], 4u);
        }
        TEST_ASSERT_TRUE(v[0] == 1.0f);           /* ch0 alive */
        TEST_ASSERT_TRUE(v[1] == 1.0f);           /* ch1 rx_frames */
        TEST_ASSERT_TRUE((int)v[9] == 0);         /* ch9 port_absent */
    }
    Link_StopTelemetry();
    printf("PASS: test_telemetry_channels\n");
    return 0;
}

/* 状态帧 TX 组帧正确（回环 oracle：自发帧喂回解析器能收到）。 */
static int test_send_state_loopback(void)
{
    uint8_t payload[5] = { 1u, 2u, 3u, 4u, 5u };
    uint8_t tx[64];
    uint8_t buf[32];
    uint8_t len = 0u;
    uint32_t n;

    setup_present();
    TEST_ASSERT_TRUE(Link_SendState(payload, 5u));
    n = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    TEST_ASSERT_TRUE(n == 12u);                   /* 7 + 5 */
    FakeWirelessPort_PushRx(tx, n);               /* 自环回（杜邦短接的主机版） */
    Link_Update(10u);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len));
    TEST_ASSERT_TRUE(len == 5u && memcmp(buf, payload, 5u) == 0);
    TEST_ASSERT_TRUE(Link_IsAlive());             /* 自环回即活性=字节层 OK 的验收剧本 */
    printf("PASS: test_send_state_loopback\n");
    return 0;
}

/* ---- v2 新增：序号/去重/梯队/事件必达 ------------------------------------- */

/* 不可靠流 TX seq 单调，STATE 与心跳共用一个计数器。 */
static int test_state_seq_monotonic_shared_with_hb(void)
{
    uint8_t p[1] = { 0x10u };
    uint8_t tx[64];

    setup_present();
    TEST_ASSERT_TRUE(Link_SendState(p, 1u));      /* seq 0 */
    TEST_ASSERT_TRUE(Link_SendState(p, 1u));      /* seq 1 */
    Link_Update(200u);                            /* 心跳到期但被数据流量抑制 */
    FakeWirelessPort_ClearTx();
    TEST_ASSERT_TRUE(Link_SendState(p, 1u));      /* 心跳被抑制 → 此帧 seq=2 */
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 8u);
    TEST_ASSERT_TRUE(tx[3] == 0x01u && tx[4] == 2u);
    printf("PASS: test_state_seq_monotonic_shared_with_hb\n");
    return 0;
}

/* 不可靠流 gap 账：seq 0 后跳 seq 3 → gap += 2（丢包率可查）。 */
static int test_ur_gap_count(void)
{
    uint8_t frame[40];
    uint8_t p[1] = { 0x01u };
    Wireless_Diag_T d;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, 0u, p, 1u);
    FakeWirelessPort_PushRx(frame, n);
    n = build_frame(frame, 0x01u, 3u, p, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.ur_gap_count == 2u && d.frame_count == 2u);
    printf("PASS: test_ur_gap_count\n");
    return 0;
}

/* 不可靠流重复 seq 排除：信箱保首帧，dup 计数，活性仍刷新。 */
static int test_ur_dup_excluded(void)
{
    uint8_t frame[40];
    uint8_t pa[1] = { 0xAAu };
    uint8_t pb[1] = { 0xBBu };
    uint8_t buf[32];
    uint8_t len = 0u;
    Wireless_Diag_T d;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, 7u, pa, 1u);
    FakeWirelessPort_PushRx(frame, n);
    n = build_frame(frame, 0x01u, 7u, pb, 1u);    /* 同 seq 重复投递 */
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.ur_dup_count == 1u);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len) && buf[0] == 0xAAu);
    TEST_ASSERT_TRUE(Link_IsAlive());             /* dup 仍证明对端活着 */
    printf("PASS: test_ur_dup_excluded\n");
    return 0;
}

/* EVENT 首发即 TX；ACK 到达清 pending，delivered 计数。 */
static int test_event_ack_clears_pending(void)
{
    uint8_t p[2] = { 0xC0u, 0x01u };
    uint8_t tx[64];
    uint8_t ack[8];
    Wireless_Diag_T d;
    uint32_t n;

    setup_present();
    TEST_ASSERT_TRUE(Link_SendEvent(p, 2u));
    TEST_ASSERT_TRUE(Link_EventBusy());
    n = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    TEST_ASSERT_TRUE(n == 9u && tx[3] == 0x03u && tx[4] == 0u);   /* EVENT seq 0 */
    n = build_frame(ack, 0x04u, 0u, (const uint8_t *)"", 0u);     /* 对端 ACK */
    FakeWirelessPort_PushRx(ack, n);
    Link_Update(10u);
    TEST_ASSERT_TRUE(!Link_EventBusy());
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.delivered_count == 1u && d.retx_count == 0u);
    printf("PASS: test_event_ack_clears_pending\n");
    return 0;
}

/* 无 ACK → 40ms(4 tick) 重传，且重传帧与首发字节级一致（同 seq）。 */
static int test_event_retransmit_same_seq(void)
{
    uint8_t p[3] = { 0xD1u, 0xD2u, 0xD3u };
    uint8_t first[16];
    uint8_t again[16];
    Wireless_Diag_T d;
    uint32_t n1, n2;

    setup_present();
    TEST_ASSERT_TRUE(Link_SendEvent(p, 3u));
    n1 = FakeWirelessPort_CopyTx(first, sizeof(first));
    FakeWirelessPort_ClearTx();
    Link_Update(10u);
    Link_Update(20u);
    Link_Update(30u);
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(again, sizeof(again)) == 0u);  /* <40ms 不重传 */
    Link_Update(40u);
    n2 = FakeWirelessPort_CopyTx(again, sizeof(again));
    TEST_ASSERT_TRUE(n2 == n1 && memcmp(first, again, n1) == 0);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.retx_count == 1u);
    printf("PASS: test_event_retransmit_same_seq\n");
    return 0;
}

/* 始终无 ACK → 8 次重传后放弃：ev_fail 计数、pending 解除、可再发新事件。 */
static int test_event_abandon_after_max_retries(void)
{
    uint8_t p[1] = { 0xEEu };
    Wireless_Diag_T d;
    uint32_t t;

    setup_present();
    TEST_ASSERT_TRUE(Link_SendEvent(p, 1u));
    for (t = 10u; t <= 1000u; t += 10u) {
        Link_Update(t);
    }
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.retx_count == 8u && d.ev_fail_count == 1u);
    TEST_ASSERT_TRUE(!Link_EventBusy());
    TEST_ASSERT_TRUE(Link_SendEvent(p, 1u));      /* 槽已释放，seq=1 的新事件可发 */
    printf("PASS: test_event_abandon_after_max_retries\n");
    return 0;
}

/* 收端 EVENT：即收即回 ACK；同 seq 重复到达 → 重发 ACK 但不重复投递。 */
static int test_event_rx_ack_and_dedup(void)
{
    uint8_t frame[40];
    uint8_t expect_ack[8];
    uint8_t p[2] = { 0x33u, 0x44u };
    uint8_t tx[64];
    uint8_t buf[32];
    uint8_t len = 0u;
    Wireless_Diag_T d;
    uint32_t n, na;

    setup_present();
    n = build_frame(frame, 0x03u, 5u, p, 2u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    na = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    (void)build_frame(expect_ack, 0x04u, 5u, (const uint8_t *)"", 0u);
    TEST_ASSERT_TRUE(na == 7u && memcmp(tx, expect_ack, 7u) == 0);  /* ACK(seq=5) */
    TEST_ASSERT_TRUE(Link_TakeEvent(buf, sizeof(buf), &len));
    TEST_ASSERT_TRUE(len == 2u && memcmp(buf, p, 2u) == 0);

    FakeWirelessPort_ClearTx();
    FakeWirelessPort_PushRx(frame, n);            /* 对端没收到 ACK，重传同帧 */
    Link_Update(20u);
    na = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    TEST_ASSERT_TRUE(na == 7u && memcmp(tx, expect_ack, 7u) == 0);  /* 重 ACK */
    TEST_ASSERT_TRUE(!Link_TakeEvent(buf, sizeof(buf), &len));      /* 不重复投递 */
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.ev_dup_count == 1u);
    printf("PASS: test_event_rx_ack_and_dedup\n");
    return 0;
}

/* 事件队列 FIFO 全收；满（深 4）→ 丢弃计数且不 ACK（对端会重传，不假确认）。 */
static int test_event_queue_fifo_and_overflow(void)
{
    uint8_t frame[40];
    uint8_t tx[64];
    uint8_t buf[32];
    uint8_t len = 0u;
    Wireless_Diag_T d;
    uint32_t n;
    uint8_t i;

    setup_present();
    for (i = 0u; i < 5u; i++) {
        uint8_t p[1];
        p[0] = (uint8_t)(0x50u + i);
        n = build_frame(frame, 0x03u, i, p, 1u);
        FakeWirelessPort_PushRx(frame, n);
    }
    Link_Update(10u);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.ev_rx_drop_count == 1u);
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 28u);  /* 只 4 个 ACK */
    for (i = 0u; i < 4u; i++) {
        TEST_ASSERT_TRUE(Link_TakeEvent(buf, sizeof(buf), &len));
        TEST_ASSERT_TRUE(len == 1u && buf[0] == (uint8_t)(0x50u + i));  /* FIFO 序 */
    }
    TEST_ASSERT_TRUE(!Link_TakeEvent(buf, sizeof(buf), &len));
    printf("PASS: test_event_queue_fifo_and_overflow\n");
    return 0;
}

/* 未知 type（CRC 合法）拒收计数，不进 frame_count、不刷活性。 */
static int test_unknown_type_rejected(void)
{
    uint8_t frame[40];
    uint8_t p[1] = { 0x99u };
    Wireless_Diag_T d;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x7Fu, 0u, p, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.unknown_type_count == 1u && d.frame_count == 0u);
    TEST_ASSERT_TRUE(!Link_IsAlive());
    printf("PASS: test_unknown_type_rejected\n");
    return 0;
}

/* 梯队带宽卫生：周期内已有成功数据 TX → 该拍心跳被抑制，下一拍恢复。 */
static int test_heartbeat_suppressed_by_traffic(void)
{
    uint8_t p[1] = { 0x42u };
    uint8_t tx[64];
    Link_Telemetry_T t;
    uint32_t n;

    setup_present();
    Link_Update(190u);
    TEST_ASSERT_TRUE(Link_SendState(p, 1u));      /* 200ms 到期前有数据帧 */
    FakeWirelessPort_ClearTx();
    Link_Update(200u);                            /* 心跳到期 → 被抑制 */
    TEST_ASSERT_TRUE(FakeWirelessPort_CopyTx(tx, sizeof(tx)) == 0u);
    for (n = 210u; n <= 400u; n += 10u) {
        Link_Update(n);                           /* 无新流量 → 400ms 心跳恢复 */
    }
    n = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    TEST_ASSERT_TRUE(n == 7u && tx[3] == 0x02u);
    Link_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.hb_sent == 1u);
    printf("PASS: test_heartbeat_suppressed_by_traffic\n");
    return 0;
}

/* 状态信箱与事件队列互不串线。 */
static int test_state_event_independent(void)
{
    uint8_t frame[40];
    uint8_t ps[1] = { 0x05u };
    uint8_t pe[1] = { 0x0Eu };
    uint8_t buf[32];
    uint8_t len = 0u;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, 0u, ps, 1u);
    FakeWirelessPort_PushRx(frame, n);
    n = build_frame(frame, 0x03u, 0u, pe, 1u);
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    TEST_ASSERT_TRUE(Link_TakeEvent(buf, sizeof(buf), &len) && buf[0] == 0x0Eu);
    TEST_ASSERT_TRUE(Link_TakeState(buf, sizeof(buf), &len) && buf[0] == 0x05u);
    printf("PASS: test_state_event_independent\n");
    return 0;
}

/* 单在途：事件未决期间再发 → false；ACK 后可发且 seq 递进。 */
static int test_event_single_inflight(void)
{
    uint8_t p[1] = { 0x71u };
    uint8_t ack[8];
    uint8_t tx[64];
    uint32_t n;

    setup_present();
    TEST_ASSERT_TRUE(Link_SendEvent(p, 1u));      /* seq 0 */
    TEST_ASSERT_TRUE(!Link_SendEvent(p, 1u));     /* 在途拒绝 */
    n = build_frame(ack, 0x04u, 0u, (const uint8_t *)"", 0u);
    FakeWirelessPort_PushRx(ack, n);
    Link_Update(10u);
    FakeWirelessPort_ClearTx();
    TEST_ASSERT_TRUE(Link_SendEvent(p, 1u));      /* seq 1 */
    n = FakeWirelessPort_CopyTx(tx, sizeof(tx));
    TEST_ASSERT_TRUE(n == 8u && tx[3] == 0x03u && tx[4] == 1u);
    printf("PASS: test_event_single_inflight\n");
    return 0;
}

/* CRC 覆盖 seq 字节：只翻 seq → 整帧拒收。 */
static int test_crc_covers_seq(void)
{
    uint8_t frame[40];
    uint8_t p[1] = { 0x21u };
    uint8_t buf[32];
    uint8_t len = 0u;
    Wireless_Diag_T d;
    uint32_t n;

    setup_present();
    n = build_frame(frame, 0x01u, 4u, p, 1u);
    frame[4] ^= 0xFFu;                            /* 只毁 seq 字节 */
    FakeWirelessPort_PushRx(frame, n);
    Link_Update(10u);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.crc_error_count == 1u && d.frame_count == 0u);
    TEST_ASSERT_TRUE(!Link_TakeState(buf, sizeof(buf), &len));
    printf("PASS: test_crc_covers_seq\n");
    return 0;
}

/* 迟到/无主 ACK：无 pending 时到达 → 忽略不崩，delivered 不涨，ur 账不动。 */
static int test_stale_ack_ignored(void)
{
    uint8_t ack[8];
    Wireless_Diag_T d;
    uint32_t n;

    setup_present();
    n = build_frame(ack, 0x04u, 9u, (const uint8_t *)"", 0u);
    FakeWirelessPort_PushRx(ack, n);
    Link_Update(10u);
    Wireless_GetDiag(&d);
    TEST_ASSERT_TRUE(d.frame_count == 1u && d.delivered_count == 0u);
    TEST_ASSERT_TRUE(d.ur_gap_count == 0u && d.ur_dup_count == 0u);
    TEST_ASSERT_TRUE(Link_IsAlive());             /* 合法帧仍刷活性 */
    printf("PASS: test_stale_ack_ignored\n");
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
    failures += test_send_state_loopback();
    failures += test_state_seq_monotonic_shared_with_hb();
    failures += test_ur_gap_count();
    failures += test_ur_dup_excluded();
    failures += test_event_ack_clears_pending();
    failures += test_event_retransmit_same_seq();
    failures += test_event_abandon_after_max_retries();
    failures += test_event_rx_ack_and_dedup();
    failures += test_event_queue_fifo_and_overflow();
    failures += test_unknown_type_rejected();
    failures += test_heartbeat_suppressed_by_traffic();
    failures += test_state_event_independent();
    failures += test_event_single_inflight();
    failures += test_crc_covers_seq();
    failures += test_stale_ack_ignored();

    if (failures != 0) {
        printf("%d link test(s) failed.\n", failures);
        return 1;
    }

    printf("All wireless link tests passed.\n");
    return 0;
}
