/**
 * @file    fake_beacon.c
 * @brief   driver/beacon 的主机侧 fake：记录电平状态（硬件边界伪装，
 *          fake_motor_hw/fake_board_gpio 同款先例）。
 */
#include <stdbool.h>

static bool s_buzzer_on;
static bool s_led_on;

void FakeBeacon_Reset(void)
{
    s_buzzer_on = false;
    s_led_on = false;
}

bool FakeBeacon_BuzzerOn(void)
{
    return s_buzzer_on;
}

bool FakeBeacon_LedOn(void)
{
    return s_led_on;
}

/* ---- driver/beacon 公共 API 的 fake 实现 -------------------------------- */

void Beacon_Init(void)
{
    s_buzzer_on = false;
    s_led_on = false;
}

void Beacon_SetBuzzer(bool on)
{
    s_buzzer_on = on;
}

void Beacon_SetLed(bool on)
{
    s_led_on = on;
}
