/**
 * @file    test_uart_check.c
 * @brief   uart_check 服务主机测试（UDIAG，契约 §39）。
 *
 * 链接组成：真实 uart_check.c + uart_vofa.c + board_uart×4 + fake_uart_port.c
 *           （RX 注入制造真溢出 + VOFA TX 抓取）+ fake_wireless_port.c（wl 端口计数）。
 * 用例思路：往真实端口 FIFO 塞超容量字节制造**真溢出计数**，再断言服务镜像与
 * Driver getter 同源同值——fake 只伪装硬件边界，不伪装被测体。
 */
#include "app/service/uart_check/uart_check.h"
#include "driver/board_uart/vofa_uart.h"
#include "driver/board_uart/imu_uart.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length);
extern void FakeUartPort_PushImuBytes(const uint8_t *data, uint32_t length);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);
extern void FakeWirelessPort_Reset(void);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static void overflow_vofa_rx(uint32_t extra)
{
    uint8_t junk[64];
    uint32_t pushed = 0u;

    memset(junk, 0x55, sizeof(junk));
    /* VOFA RX FIFO 512B：先灌满，再多灌 extra 字节制造溢出计数。 */
    while (pushed < (512u + extra)) {
        uint32_t n = (512u + extra) - pushed;
        if (n > sizeof(junk)) {
            n = sizeof(junk);
        }
        FakeUartPort_PushVofaBytes(junk, n);
        pushed += n;
    }
}

static void setup(void)
{
    FakeUartPort_ResetAll();
    FakeWirelessPort_Reset();
    (void)vofa_init();
}

/* 六计数镜像与 Driver getter 同源同值（vofa 真溢出非零信号）。 */
static int test_mirror_matches_sources(void)
{
    UartCheck_Telemetry_T t;

    setup();
    overflow_vofa_rx(7u);
    UartCheck_Start();
    UartCheck_Update(0u);       /* 首拍播种 */
    UartCheck_Update(10u);
    UartCheck_GetTelemetry(&t);
    TEST_ASSERT_TRUE(t.vofa_rx_ovf == 7u);
    TEST_ASSERT_TRUE(t.vofa_rx_ovf == VofaUart_GetRxOverflowCount());
    TEST_ASSERT_TRUE(t.vofa_tx_ovf == VofaUart_GetTxOverflowCount());
    TEST_ASSERT_TRUE(t.imu_rx_ovf == ImuUart_GetRxOverflowCount());
    TEST_ASSERT_TRUE(t.vision_rx_ovf == 0u && t.step_rx_ovf == 0u && t.wl_rx_ovf == 0u);
    UartCheck_Stop();
    printf("PASS: test_mirror_matches_sources\n");
    return 0;
}

/* VOFA 遥测帧 = 6 通道 JustFloat（28 字节），ch0 数值=vofa_rx_ovf。 */
static int test_frame_six_channels(void)
{
    uint8_t raw[6u * 4u + 4u + 16u];
    float v0;
    uint32_t n;

    setup();
    overflow_vofa_rx(3u);
    UartCheck_Start();
    UartCheck_Update(0u);
    UartCheck_Update(10u);
    n = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    TEST_ASSERT_TRUE(n == (6u * 4u + 4u));
    memcpy(&v0, &raw[0], 4u);
    TEST_ASSERT_TRUE(v0 == 3.0f);
    UartCheck_Stop();
    printf("PASS: test_frame_six_channels\n");
    return 0;
}

/* Stop 清表 + 重进不累积：第二次会话仍是 6 通道帧（非 12）。 */
static int test_stop_start_no_channel_accumulation(void)
{
    uint8_t raw[16u * 4u];
    uint32_t n;

    setup();
    UartCheck_Start();
    UartCheck_Update(0u);
    UartCheck_Update(10u);
    UartCheck_Stop();
    UartCheck_Start();
    UartCheck_Update(20u);      /* 重进首拍重新播种 */
    UartCheck_Update(30u);
    n = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    TEST_ASSERT_TRUE(n == (6u * 4u + 4u));
    UartCheck_Stop();
    printf("PASS: test_stop_start_no_channel_accumulation\n");
    return 0;
}

/* 未 Start 时 Update 无副作用（不发帧、不崩）。 */
static int test_update_without_start_inert(void)
{
    uint8_t raw[64];

    setup();
    UartCheck_Update(0u);
    UartCheck_Update(10u);
    UartCheck_Update(20u);
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u);
    printf("PASS: test_update_without_start_inert\n");
    return 0;
}

/* 计数是 Driver 累计型：跨 Stop/Start 会话保留且只增不清（证据不因换页蒸发）。
 * 注意 vofa_run 会排空 VOFA RX 解析命令（职责内），FIFO 空余量不可预期——
 * 第二轮灌 2×512+7 字节保证至少 512 字节溢出（即使 FIFO 被排空到全空）。 */
static int test_counters_survive_sessions(void)
{
    UartCheck_Telemetry_T t1;
    UartCheck_Telemetry_T t2;

    setup();
    overflow_vofa_rx(5u);
    UartCheck_Start();
    UartCheck_Update(0u);
    UartCheck_Update(10u);
    UartCheck_GetTelemetry(&t1);
    TEST_ASSERT_TRUE(t1.vofa_rx_ovf == 5u);
    UartCheck_Stop();
    overflow_vofa_rx(512u + 7u);
    UartCheck_Start();
    UartCheck_Update(20u);
    UartCheck_Update(30u);
    UartCheck_GetTelemetry(&t2);
    TEST_ASSERT_TRUE(t2.vofa_rx_ovf >= (t1.vofa_rx_ovf + 512u));
    TEST_ASSERT_TRUE(t2.vofa_rx_ovf == VofaUart_GetRxOverflowCount());
    UartCheck_Stop();
    printf("PASS: test_counters_survive_sessions\n");
    return 0;
}

/* NULL 安全。 */
static int test_null_safe(void)
{
    setup();
    UartCheck_GetTelemetry((UartCheck_Telemetry_T *)0);
    printf("PASS: test_null_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_mirror_matches_sources();
    failures += test_frame_six_channels();
    failures += test_stop_start_no_channel_accumulation();
    failures += test_update_without_start_inert();
    failures += test_counters_survive_sessions();
    failures += test_null_safe();

    if (failures != 0) {
        printf("%d uart_check test(s) failed.\n", failures);
        return 1;
    }

    printf("All uart_check tests passed.\n");
    return 0;
}
