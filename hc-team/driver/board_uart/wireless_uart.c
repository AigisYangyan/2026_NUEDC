/**
 * @file    wireless_uart.c
 * @brief   ESP32-C3 UART 端口占位实现（H6 引脚定案后整体替换，接口不变）。
 *
 * 如实上报端口缺席：Init false / Read 0 / Write false。上层 uart_wireless 的
 * diag.port_absent 与 LinkTest 遥测据此显示 1，不伪装链路存在。
 */
#include "driver/board_uart/wireless_uart.h"

bool WirelessUart_Init(void)
{
    return false;   /* 端口缺席（引脚未定案） */
}

uint32_t WirelessUart_Read(uint8_t *buf, uint32_t cap)
{
    (void)buf;
    (void)cap;
    return 0u;
}

bool WirelessUart_Write(const uint8_t *data, uint32_t len)
{
    (void)data;
    (void)len;
    return false;
}

uint32_t WirelessUart_GetRxOverflowCount(void)
{
    return 0u;
}
