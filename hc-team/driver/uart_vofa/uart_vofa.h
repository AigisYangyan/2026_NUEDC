#ifndef __UART_HOST_LINK_H__
#define __UART_HOST_LINK_H__

/*******************************************************************************
 * @file        uart_vofa_v2.h
 * @brief       VOFA+上位机串口通信协议库 (Decoupled V2)
 * @version     V2.2
 * @date        2026-07-16
 * @author      eternal_fu, 中性粒
 * @note        传输改为 board_uart/vofa_uart；RX 解析只在 vofa_run() 任务上下文发生
 *******************************************************************************/

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;

#define VOFA_PROTOCOL_FIREWATER 0
#define VOFA_PROTOCOL_JUSTFLOAT 1

#define DEVICE_TYPE_STM32   1
#define DEVICE_TYPE_ESP32   2
#define DEVICE_TYPE_MSPM0   3

#ifndef DEVICE_TYPE
#define DEVICE_TYPE DEVICE_TYPE_MSPM0
#endif

#define VOFA_PROTOCOL_SELECT VOFA_PROTOCOL_JUSTFLOAT

#define VOFA_TX_BUF_SIZE    512
#define VOFA_RX_BUF_SIZE    128
#define VOFA_CHANNEL_MAX    16
#define VOFA_RX_PARAM_MAX   32

#define VOFA_JUSTFLOAT_TAIL {0x00, 0x00, 0x80, 0x7f}

typedef void (*vofa_param_setter_t)(float value);
typedef void (*vofa_send_fn_t)(uint8_t *data, uint16_t len);

int vofa_init(void);
void vofa_clear_profile(void);
int vofa_register_float(float *data);
int vofa_register_int(int *data);
int vofa_bind_cmd(const char *cmd, volatile float *val_ptr);
void vofa_run(void);

#endif
