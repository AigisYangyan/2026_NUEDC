/**
 * @file    alert.c
 * @brief   声光提示服务实现：语义节拍相位机（相位/节拍唯一所有者）。
 *
 * 输出唯一经 driver/beacon 电平开关；本服务只算「此刻该亮/该响吗」。
 * 一次性模式播完自动回 NONE（自灭）；持续模式由整除相位驱动（抖动免疫）。
 */
#include "app/service/alert/alert.h"

#include "driver/beacon/beacon.h"

/* 节拍常量（ms）——验收动作是人耳人眼，编译期定值，现场不调（契约 §33）。 */
#define ALERT_BEEP_SHORT_MS   100u
#define ALERT_BEEP_LONG_MS    500u
#define ALERT_BLINK_SLOW_MS   500u
#define ALERT_BLINK_FAST_MS   100u
#define ALERT_BEEP_BLINK_MS   250u

static Alert_Pattern_T s_pattern;
static bool            s_seeded;
static uint32_t        s_phase_base_ms;

/** 全灭 + 回 NONE（Play 换模式/Stop/一次性播完共用的唯一静默点）。 */
static void alert_silence(void)
{
    Beacon_SetBuzzer(false);
    Beacon_SetLed(false);
    s_pattern = ALERT_PATTERN_NONE;
    s_seeded = false;
}

void Alert_Init(void)
{
    alert_silence();
}

void Alert_Play(Alert_Pattern_T pattern)
{
    /* 换模式即重置相位与输出（无跨模式残留）；越界输入按 NONE 处理。 */
    alert_silence();
    if ((pattern > ALERT_PATTERN_NONE) && (pattern <= ALERT_PATTERN_BEEP_BLINK)) {
        s_pattern = pattern;
    }
}

void Alert_Update(uint32_t now_ms)
{
    uint32_t t;

    if (s_pattern == ALERT_PATTERN_NONE) {
        return;
    }
    if (!s_seeded) {
        s_seeded = true;
        s_phase_base_ms = now_ms;
    }
    t = now_ms - s_phase_base_ms;   /* 相位内时刻；无符号减法天然处理回绕 */

    switch (s_pattern) {
    case ALERT_PATTERN_BEEP_SHORT:
        if (t < ALERT_BEEP_SHORT_MS) {
            Beacon_SetBuzzer(true);
        } else {
            alert_silence();        /* 播完自灭 */
        }
        break;
    case ALERT_PATTERN_BEEP_DOUBLE:
        if (t < 100u) {
            Beacon_SetBuzzer(true);
        } else if (t < 200u) {
            Beacon_SetBuzzer(false);
        } else if (t < 300u) {
            Beacon_SetBuzzer(true);
        } else {
            alert_silence();
        }
        break;
    case ALERT_PATTERN_BEEP_LONG:
        if (t < ALERT_BEEP_LONG_MS) {
            Beacon_SetBuzzer(true);
        } else {
            alert_silence();
        }
        break;
    case ALERT_PATTERN_LED_ON:
        Beacon_SetLed(true);
        break;
    case ALERT_PATTERN_BLINK_SLOW:
        Beacon_SetLed(((t / ALERT_BLINK_SLOW_MS) % 2u) == 0u);
        break;
    case ALERT_PATTERN_BLINK_FAST:
        Beacon_SetLed(((t / ALERT_BLINK_FAST_MS) % 2u) == 0u);
        break;
    case ALERT_PATTERN_BEEP_BLINK: {
        bool phase_on = ((t / ALERT_BEEP_BLINK_MS) % 2u) == 0u;
        Beacon_SetBuzzer(phase_on);
        Beacon_SetLed(phase_on);
        break;
    }
    case ALERT_PATTERN_NONE:        /* 已在函数头早退；显式列出让 -Wswitch 兜住未来加模式漏 case */
        break;
    }
}

void Alert_Stop(void)
{
    alert_silence();
}

bool Alert_IsBusy(void)
{
    return s_pattern != ALERT_PATTERN_NONE;
}
