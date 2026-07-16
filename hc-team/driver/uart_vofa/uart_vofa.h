#ifndef __UART_HOST_LINK_H__
#define __UART_HOST_LINK_H__

/*******************************************************************************
 * @file        uart_vofa_v2.h
 * @brief       VOFA+上位机串口通信协议库 (Decoupled V2)
 * @version     V2.1
 * @date        2025-11-23
 * @author      eternal_fu, 中性粒
 * @note        完全解耦PID，提供通用注册接口，支持int/float混合发送
 *******************************************************************************/

/* 包含标准类型定义 */
#include <stdint.h>
#include <stdbool.h>

/* 兼容性类型定义 */
typedef uint8_t         u8;

/***************************************************************************************************************
 *                                               协议选择与配置
 **************************************************************************************************************/
#define VOFA_PROTOCOL_FIREWATER 0   /* FireWater协议(CSV文本格式) */
#define VOFA_PROTOCOL_JUSTFLOAT 1   /* JustFloat协议(二进制浮点格式) */

/* 平台类型定义 */
#define DEVICE_TYPE_STM32   1
#define DEVICE_TYPE_ESP32   2
#define DEVICE_TYPE_MSPM0   3
/* ... 其他平台 ... */

/* 当前平台配置 (请在此修改或在工程中定义) */
#ifndef DEVICE_TYPE
#define DEVICE_TYPE         DEVICE_TYPE_MSPM0
#endif

/* 选择当前使用的协议 */
#define VOFA_PROTOCOL_SELECT    VOFA_PROTOCOL_JUSTFLOAT

/* 缓冲区配置 */
#define VOFA_TX_BUF_SIZE        512     /* 发送缓冲区大小 (字节), 需大于最大单帧长度 */
#define VOFA_RX_BUF_SIZE        128     /* 接收缓冲区大小 (字节), 环形缓冲区 */
#define VOFA_CHANNEL_MAX        16      /* 最大发送通道数 (对应 vofa_regist_send_xxx 的 ch_id 范围) */
#define VOFA_RX_PARAM_MAX       32      /* 最大接收参数数 (对应 vofa_regist_recv_param 的最大注册数) */

/* JustFloat协议帧尾 */
#define VOFA_JUSTFLOAT_TAIL     {0x00, 0x00, 0x80, 0x7f}

/***************************************************************************************************************
 *                                               回调函数类型定义
 **************************************************************************************************************/
/* 参数设置回调 - 用于接收命令 */
typedef void (*vofa_param_setter_t)(float value);

/* 底层发送函数指针类型 */
typedef void (*vofa_send_fn_t)(uint8_t *data, uint16_t len);

/***************************************************************************************************************
 *                                               核心API
 **************************************************************************************************************/

/**
 * @brief VOFA初始化
 * @return 0=成功, -1=失败
 * @note  重置所有内部状态，清空注册表，并注册底层发送函数
 */
int vofa_init(void);

/**
 * @brief 清空当前 VOFA profile
 * @note  会同时清空发送通道、接收命令绑定与半帧解析缓存。
 */
void vofa_clear_profile(void);

/**
 * @brief 注册发送数据 (Float型)
 * @param data  数据指针
 * @return 0=成功, -1=失败
 */
int vofa_register_float(float *data);

/**
 * @brief 注册发送数据 (Int型)
 * @param data  数据指针
 * @return 0=成功, -1=失败
 * @note  VOFA内部会自动将int转换为float发送
 */
int vofa_register_int(int *data);

/**
 * @brief 绑定接收命令与变量
 * @param cmd      命令字符串 (例如 "P1", "pid.p")
 * @param val_ptr  绑定的变量指针
 * @return 0=成功, -1=失败
 */
int vofa_bind_cmd(const char *cmd, volatile float *val_ptr);

/**
 * @brief 串口接收中断处理函数
 * @param byte 接收到的字节
 * @note  在串口接收中断中调用
 */
void vofa_rx_isr(uint8_t byte);

/**
 * @brief VOFA主运行函数 (发送所有数据)
 * @note  遍历并发送所有已注册的通道数据
 *        建议配合定时器调用 (例如每10ms调用一次)
 */
void vofa_run(void);

#endif
