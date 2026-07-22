/**
 * @file    beacon.c
 * @brief   声光指示 Driver 实现：DL GPIO 电平开关（本模块是唯一引脚写点）。
 */
#include "driver/beacon/beacon.h"

#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_gpio.h>

void Beacon_Init(void)
{
    Beacon_SetBuzzer(false);
    Beacon_SetLed(false);
}

void Beacon_SetBuzzer(bool on)
{
    if (on) {
        DL_GPIO_setPins(GPIO_BEACON_PORT, GPIO_BEACON_BUZZER_PIN);
    } else {
        DL_GPIO_clearPins(GPIO_BEACON_PORT, GPIO_BEACON_BUZZER_PIN);
    }
}

void Beacon_SetLed(bool on)
{
    if (on) {
        DL_GPIO_setPins(GPIO_STATUS_LED_PORT, GPIO_STATUS_LED_PIN_22_PIN);
    } else {
        DL_GPIO_clearPins(GPIO_STATUS_LED_PORT, GPIO_STATUS_LED_PIN_22_PIN);
    }
}
