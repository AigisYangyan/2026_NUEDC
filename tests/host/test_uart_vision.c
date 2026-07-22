#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/board_uart/vision_uart.h"
#include "driver/uart_vision/uart_vision.h"

void FakeUartPort_ResetAll(void);
void FakeUartPort_PushVisionBytes(const uint8_t *data, uint32_t length);
void FakeUartPort_CompleteVisionTx(void);
uint32_t FakeUartPort_CopyVisionTx(uint8_t *out, uint32_t capacity);

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

static bool float_eq(float a, float b)
{
    float diff = a - b;
    if (diff < 0.0f) {
        diff = -diff;
    }
    return (diff <= 1e-4f);
}

/* 独立复算 CRC16-MODBUS，不复用被测实现，作为交叉校验。 */
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

/* 组一帧坐标控制帧：0xAA 0x55 | len | [cmd | x float | y float] | CRC16 LE。
 * 返回整帧长度。cmd/len 由参数给，便于构造未知 cmd / 坏 CRC 用例。 */
static uint32_t build_frame(uint8_t *buf, uint8_t cmd, const uint8_t *payload_ext,
                            uint8_t ext_len, bool corrupt_crc)
{
    uint8_t body[1u + 1u + 16u];
    uint16_t crc;
    uint8_t len = (uint8_t)(1u + ext_len);
    uint8_t n = 0u;
    uint8_t i;

    buf[n++] = 0xAAu;
    buf[n++] = 0x55u;
    buf[n++] = len;
    buf[n++] = cmd;
    for (i = 0u; i < ext_len; i++) {
        buf[n++] = payload_ext[i];
    }

    /* CRC 范围 = 长度字节 + payload。 */
    body[0] = len;
    body[1] = cmd;
    for (i = 0u; i < ext_len; i++) {
        body[2u + i] = payload_ext[i];
    }
    crc = ref_crc16(body, (uint16_t)(2u + ext_len));
    if (corrupt_crc) {
        crc = (uint16_t)(crc ^ 0xFFFFu);
    }

    buf[n++] = (uint8_t)(crc & 0xFFu);
    buf[n++] = (uint8_t)((crc >> 8) & 0xFFu);
    return n;
}

static uint32_t build_coord_frame(uint8_t *buf, float x, float y, bool corrupt_crc)
{
    uint8_t ext[8];
    memcpy(&ext[0], &x, sizeof(float));
    memcpy(&ext[4], &y, sizeof(float));
    return build_frame(buf, 0x01u, ext, 8u, corrupt_crc);
}

static void reset_all(void)
{
    FakeUartPort_ResetAll();
    UartVision_Init();
}

static void test_init_silent(void)
{
    UartVision_Coord_T coord;
    uint8_t snapshot[8];

    reset_all();

    expect_true(UartVision_GetLatestCoord(&coord) == false,
                "init: no coord before any frame");
    expect_true(UartVision_GetCoordSeq() == 0u,
                "init: coord seq is zero");
    expect_true(UartVision_GetTopicAckSeq() == 0u,
                "init: ack seq is zero");
    expect_true(VisionUart_IsTxIdle() == true,
                "init: tx idle");
    expect_true(FakeUartPort_CopyVisionTx(snapshot, sizeof(snapshot)) == 0u,
                "init: no tx bytes emitted");
}

static void test_valid_coord_frame(void)
{
    uint8_t frame[16];
    uint32_t len;
    UartVision_Coord_T coord;

    reset_all();
    len = build_coord_frame(frame, 100.5f, -200.25f, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();

    expect_true(UartVision_GetCoordSeq() == 1u,
                "coord: valid frame bumps seq to 1");
    expect_true(UartVision_GetLatestCoord(&coord) == true,
                "coord: latest available after valid frame");
    expect_true(float_eq(coord.x, 100.5f) && float_eq(coord.y, -200.25f),
                "coord: float32 x/y decoded precisely");
}

static void test_bad_crc_rejected(void)
{
    uint8_t frame[16];
    uint32_t len;
    UartVision_Coord_T coord;

    reset_all();
    len = build_coord_frame(frame, 12.0f, 34.0f, true /* corrupt */);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();

    expect_true(UartVision_GetCoordSeq() == 0u,
                "coord: bad CRC leaves seq unchanged");
    expect_true(UartVision_GetLatestCoord(&coord) == false,
                "coord: bad CRC yields no coordinate");
}

static void test_split_feed_reassembles(void)
{
    uint8_t frame[16];
    uint32_t len;
    UartVision_Coord_T coord;

    reset_all();
    len = build_coord_frame(frame, 7.0f, 8.0f, false);

    FakeUartPort_PushVisionBytes(frame, 7u); /* partial */
    UartVision_Poll();
    expect_true(UartVision_GetCoordSeq() == 0u,
                "coord: partial frame does not complete");

    FakeUartPort_PushVisionBytes(&frame[7], len - 7u); /* remainder */
    UartVision_Poll();
    expect_true((UartVision_GetCoordSeq() == 1u) &&
                    (UartVision_GetLatestCoord(&coord) == true) &&
                    float_eq(coord.x, 7.0f) && float_eq(coord.y, 8.0f),
                "coord: split feed reassembles into one frame");
}

static void test_leading_garbage_resync(void)
{
    uint8_t stream[24];
    uint8_t frame[16];
    uint32_t flen;
    uint32_t n = 0u;
    UartVision_Coord_T coord;

    reset_all();
    /* 前置垃圾：含一个 0xAA 但后随非 0x55，以及杂字节，考验重扫。 */
    stream[n++] = 0xAAu;
    stream[n++] = 0x00u;
    stream[n++] = 0x11u;
    stream[n++] = 0xFFu; /* 0xFF 起头但非合法握手（后随非 0xFE）→ 应丢弃 */
    stream[n++] = 0x22u;

    flen = build_coord_frame(frame, -1.5f, 2.5f, false);
    memcpy(&stream[n], frame, flen);
    n += flen;

    FakeUartPort_PushVisionBytes(stream, n);
    UartVision_Poll();

    expect_true((UartVision_GetCoordSeq() == 1u) &&
                    (UartVision_GetLatestCoord(&coord) == true) &&
                    float_eq(coord.x, -1.5f) && float_eq(coord.y, 2.5f),
                "coord: resync past leading garbage");
}

static void test_unknown_cmd_ignored(void)
{
    uint8_t frame[16];
    uint32_t len;
    uint8_t ext[2] = {0xABu, 0xCDu};

    reset_all();
    /* 合法 CRC 但 cmd=0x03（未定义）→ 静默丢弃（0x02 已在 V1 §36 解冻为状态帧）。 */
    len = build_frame(frame, 0x03u, ext, 2u, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();

    expect_true((UartVision_GetCoordSeq() == 0u) && (UartVision_GetStatusSeq() == 0u),
                "coord: valid-CRC unknown cmd updates nothing");
}

static void test_status_frame_parsed(void)
{
    uint8_t frame[16];
    uint32_t len;
    uint8_t ext[2] = {0x0Au, 0x0Bu};
    uint8_t st[2] = {0u, 0u};

    reset_all();
    expect_true(UartVision_GetLatestStatus(st) == false,
                "status: none before any frame");
    len = build_frame(frame, 0x02u, ext, 2u, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();
    expect_true((UartVision_GetStatusSeq() == 1u) &&
                    (UartVision_GetLatestStatus(st) == true) &&
                    (st[0] == 0x0Au) && (st[1] == 0x0Bu),
                "status: 0x02 frame parsed verbatim with seq 1");
}

static void test_status_bad_len_dropped(void)
{
    uint8_t frame[16];
    uint32_t len;
    uint8_t ext[3] = {0x01u, 0x02u, 0x03u};

    reset_all();
    /* 合法 CRC 但 cmd=0x02 len=4（≠3）→ 丢弃不更新。 */
    len = build_frame(frame, 0x02u, ext, 3u, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();
    expect_true(UartVision_GetStatusSeq() == 0u,
                "status: wrong-length 0x02 frame dropped");
}

static void test_status_latest_wins_and_independent(void)
{
    uint8_t frame[16];
    uint32_t len;
    uint8_t ext1[2] = {0x11u, 0x22u};
    uint8_t ext2[2] = {0x33u, 0x44u};
    uint8_t st[2] = {0u, 0u};

    reset_all();
    len = build_frame(frame, 0x02u, ext1, 2u, false);
    FakeUartPort_PushVisionBytes(frame, len);
    len = build_frame(frame, 0x02u, ext2, 2u, false);
    FakeUartPort_PushVisionBytes(frame, len);
    len = build_coord_frame(frame, 9.0f, 8.0f, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();
    expect_true((UartVision_GetStatusSeq() == 2u) &&
                    (UartVision_GetLatestStatus(st) == true) &&
                    (st[0] == 0x33u) && (st[1] == 0x44u),
                "status: latest wins with seq 2");
    expect_true(UartVision_GetCoordSeq() == 1u,
                "status: coord/status seq independent");
}

static void test_two_frames_latest_wins(void)
{
    uint8_t frame[16];
    uint32_t len;
    UartVision_Coord_T coord;

    reset_all();
    len = build_coord_frame(frame, 1.0f, 2.0f, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();

    len = build_coord_frame(frame, 3.0f, 4.0f, false);
    FakeUartPort_PushVisionBytes(frame, len);
    UartVision_Poll();

    expect_true(UartVision_GetCoordSeq() == 2u,
                "coord: two frames bump seq to 2");
    expect_true((UartVision_GetLatestCoord(&coord) == true) &&
                    float_eq(coord.x, 3.0f) && float_eq(coord.y, 4.0f),
                "coord: latest frame wins");
}

static void test_send_topic_frame_bytes(void)
{
    uint8_t snapshot[8];
    uint32_t len;

    reset_all();
    expect_true(UartVision_SendTopic(0x02u, 0x01u) == true,
                "topic: send accepted when tx idle");
    len = FakeUartPort_CopyVisionTx(snapshot, sizeof(snapshot));
    expect_true((len == 4u) && (snapshot[0] == 0xFFu) && (snapshot[1] == 0x02u) &&
                    (snapshot[2] == 0x01u) && (snapshot[3] == 0xFEu),
                "topic: exact 0xFF main sub 0xFE on the wire");
}

static void test_send_topic_busy_rejected(void)
{
    uint8_t snapshot[8];

    reset_all();
    expect_true(UartVision_SendTopic(0x01u, 0x00u) == true,
                "topic: first send arms tx");
    expect_true(UartVision_SendTopic(0x03u, 0x02u) == false,
                "topic: second send rejected while tx busy");
    (void)FakeUartPort_CopyVisionTx(snapshot, sizeof(snapshot));
    expect_true((snapshot[1] == 0x01u) && (snapshot[2] == 0x00u),
                "topic: busy reject keeps first frame in buffer");
}

static void test_tx_done_lifecycle(void)
{
    reset_all();
    expect_true(UartVision_SendTopic(0x01u, 0x02u) == true,
                "tx: send arms tx");
    expect_true(VisionUart_IsTxIdle() == false,
                "tx: not idle while busy");
    FakeUartPort_CompleteVisionTx();
    expect_true(VisionUart_ConsumeTxDone() == true,
                "tx: done observed once");
    expect_true((VisionUart_ConsumeTxDone() == false) &&
                    (VisionUart_IsTxIdle() == true),
                "tx: second consume false and tx idle");
}

static void test_topic_ack_rx(void)
{
    uint8_t ack_frame[4] = {0xFFu, 0x02u, 0x01u, 0xFEu};
    uint8_t main_task = 0u;
    uint8_t sub_task = 0u;

    reset_all();
    expect_true(UartVision_GetTopicAck(&main_task, &sub_task) == false,
                "ack: none before any confirmation");

    FakeUartPort_PushVisionBytes(ack_frame, sizeof(ack_frame));
    UartVision_Poll();

    expect_true((UartVision_GetTopicAckSeq() == 1u) &&
                    (UartVision_GetTopicAck(&main_task, &sub_task) == true) &&
                    (main_task == 0x02u) && (sub_task == 0x01u),
                "ack: confirmation frame echoes task numbers");
}

int main(void)
{
    test_init_silent();
    test_valid_coord_frame();
    test_bad_crc_rejected();
    test_split_feed_reassembles();
    test_leading_garbage_resync();
    test_unknown_cmd_ignored();
    test_status_frame_parsed();
    test_status_bad_len_dropped();
    test_status_latest_wins_and_independent();
    test_two_frames_latest_wins();
    test_send_topic_frame_bytes();
    test_send_topic_busy_rejected();
    test_tx_done_lifecycle();
    test_topic_ack_rx();

    printf("All uart_vision tests passed (%d tests).\n", s_test_count);
    return 0;
}
