/**
 * @file    test_alert.c
 * @brief   声光提示服务主机测试（B1，契约 §33）。
 *
 * 链接组成：真实 alert.c + fake_beacon.c（Driver 硬件边界伪装）。
 * alert 取 now_ms 参数注入，不直读 Clock。
 */
#include "app/service/alert/alert.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* fake 抓取接口 */
extern void FakeBeacon_Reset(void);
extern bool FakeBeacon_BuzzerOn(void);
extern bool FakeBeacon_LedOn(void);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static void setup(void)
{
    FakeBeacon_Reset();
    Alert_Init();
}

/* Init 全灭、不忙。 */
static int test_init_all_off(void)
{
    setup();
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !FakeBeacon_LedOn());
    TEST_ASSERT_TRUE(!Alert_IsBusy());
    printf("PASS: test_init_all_off\n");
    return 0;
}

/* 单响：0~99ms 响，100ms 自灭 + 不忙。 */
static int test_beep_short_sequence(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BEEP_SHORT);
    TEST_ASSERT_TRUE(Alert_IsBusy());
    Alert_Update(1000u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());
    Alert_Update(1050u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());
    Alert_Update(1100u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn());
    TEST_ASSERT_TRUE(!Alert_IsBusy());
    printf("PASS: test_beep_short_sequence\n");
    return 0;
}

/* 双响完整时序：on/off/on/自灭。 */
static int test_beep_double_full_sequence(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BEEP_DOUBLE);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());       /* 0~99 响 */
    Alert_Update(150u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn());      /* 100~199 静 */
    Alert_Update(250u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());       /* 200~299 响 */
    Alert_Update(300u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn());      /* 播完自灭 */
    TEST_ASSERT_TRUE(!Alert_IsBusy());
    printf("PASS: test_beep_double_full_sequence\n");
    return 0;
}

/* 长响：0~499ms 响，500ms 自灭。 */
static int test_beep_long(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BEEP_LONG);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());
    Alert_Update(499u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());
    Alert_Update(500u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !Alert_IsBusy());
    printf("PASS: test_beep_long\n");
    return 0;
}

/* 常亮：持续保持 + 恒忙。 */
static int test_led_on_continuous(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_LED_ON);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());
    Alert_Update(5000u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn() && Alert_IsBusy());
    printf("PASS: test_led_on_continuous\n");
    return 0;
}

/* 慢闪：500ms 半周期。 */
static int test_blink_slow_half_period(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BLINK_SLOW);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());          /* 相位 0：亮 */
    Alert_Update(500u);
    TEST_ASSERT_TRUE(!FakeBeacon_LedOn());         /* 相位 1：灭 */
    Alert_Update(1000u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());          /* 相位 2：亮 */
    printf("PASS: test_blink_slow_half_period\n");
    return 0;
}

/* 快闪：100ms 半周期。 */
static int test_blink_fast_half_period(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BLINK_FAST);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());
    Alert_Update(100u);
    TEST_ASSERT_TRUE(!FakeBeacon_LedOn());
    Alert_Update(200u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());
    printf("PASS: test_blink_fast_half_period\n");
    return 0;
}

/* 声光同步：250ms 半周期，蜂鸣器与 LED 同相。 */
static int test_beep_blink_synced(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BEEP_BLINK);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn() && FakeBeacon_LedOn());
    Alert_Update(250u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !FakeBeacon_LedOn());
    Alert_Update(500u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn() && FakeBeacon_LedOn());
    printf("PASS: test_beep_blink_synced\n");
    return 0;
}

/* Play 换模式：重置相位 + 清旧输出（无跨模式残留）。 */
static int test_play_switch_resets_phase_and_outputs(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_LED_ON);
    Alert_Update(1000u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());
    Alert_Play(ALERT_PATTERN_BEEP_SHORT);          /* 换模式 */
    TEST_ASSERT_TRUE(!FakeBeacon_LedOn());         /* 旧 LED 立即清 */
    Alert_Update(5000u);                           /* 新相位基准 = 5000 */
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn());       /* 相位 0：响（非旧基准的 4000ms） */
    Alert_Update(5100u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !Alert_IsBusy());
    printf("PASS: test_play_switch_resets_phase_and_outputs\n");
    return 0;
}

/* 未 Play 时 Update 零输出。 */
static int test_update_without_play_no_output(void)
{
    setup();
    Alert_Update(100u);
    Alert_Update(1000u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !FakeBeacon_LedOn());
    printf("PASS: test_update_without_play_no_output\n");
    return 0;
}

/* Play(NONE) 等效 Stop；越界值按 NONE 处理。 */
static int test_play_none_and_out_of_range(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_LED_ON);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_LedOn());
    Alert_Play(ALERT_PATTERN_NONE);
    TEST_ASSERT_TRUE(!FakeBeacon_LedOn() && !Alert_IsBusy());

    Alert_Play((Alert_Pattern_T)99);               /* 越界 → NONE */
    Alert_Update(100u);
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !FakeBeacon_LedOn() && !Alert_IsBusy());
    printf("PASS: test_play_none_and_out_of_range\n");
    return 0;
}

/* Stop 确定性静默。 */
static int test_stop_silences(void)
{
    setup();
    Alert_Play(ALERT_PATTERN_BEEP_BLINK);
    Alert_Update(0u);
    TEST_ASSERT_TRUE(FakeBeacon_BuzzerOn() && FakeBeacon_LedOn());
    Alert_Stop();
    TEST_ASSERT_TRUE(!FakeBeacon_BuzzerOn() && !FakeBeacon_LedOn() && !Alert_IsBusy());
    printf("PASS: test_stop_silences\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_all_off();
    failures += test_beep_short_sequence();
    failures += test_beep_double_full_sequence();
    failures += test_beep_long();
    failures += test_led_on_continuous();
    failures += test_blink_slow_half_period();
    failures += test_blink_fast_half_period();
    failures += test_beep_blink_synced();
    failures += test_play_switch_resets_phase_and_outputs();
    failures += test_update_without_play_no_output();
    failures += test_play_none_and_out_of_range();
    failures += test_stop_silences();

    if (failures != 0) {
        printf("%d alert service test(s) failed.\n", failures);
        return 1;
    }

    printf("All alert service tests passed.\n");
    return 0;
}
