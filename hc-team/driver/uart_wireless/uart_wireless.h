/**
 * @file    uart_wireless.h
 * @brief   无线链路 Driver：MCU↔ESP32-C3 UART 帧编解码 + 最新帧信箱 + 诊断。
 *
 * 协议（WL1 契约 §35.1，对称双向）：
 *   0xA5 0x5A | len(1B=payload长) | type(1B) | payload(≤32B) | CRC16-MODBUS(2B 小端)
 *   CRC 范围 = len+type+payload。type：0x01=用户数据（透传对端），0x02=心跳（空载）。
 *
 * 本层刻意不做的事（Q2/Q7 分层先例）：无时间轴、无心跳节拍、无活性判定——
 * 全归 app/service/link。ESP-NOW 组网对本层透明（ESP32-C3 侧固件负责）。
 * 调用上下文：全部任务态（端口 ISR 只搬字节进 FIFO）。
 */
#ifndef UART_WIRELESS_H
#define UART_WIRELESS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIRELESS_MAX_PAYLOAD 32u

typedef struct {
    uint32_t frame_count;     /**< 通过 CRC 的有效帧总数（用户+心跳，活性依据）。 */
    uint32_t crc_error_count; /**< CRC 不符或 len 越界被拒的帧数。 */
    uint32_t rx_overflows;    /**< 端口 RX 溢出（镜像 WirelessUart_GetRxOverflowCount；占位端口恒 0）。 */
    bool     port_absent;     /**< true=端口占位/初始化失败（引脚未定案期恒 true）。 */
} Wireless_Diag_T;

/** @brief 解析器/信箱/诊断复位 + 端口初始化（结果记入 port_absent）。 */
void Wireless_Init(void);

/** @brief 排空端口 RX 并解析（自同步分帧）。任务态周期调用。 */
void Wireless_Poll(void);

/** @brief 组帧发送用户数据（type=0x01）。len>WIRELESS_MAX_PAYLOAD 或端口缺席→false。 */
bool Wireless_SendUser(const uint8_t *data, uint8_t len);

/** @brief 组帧发送心跳（type=0x02，空载）。端口缺席→false。 */
bool Wireless_SendHeartbeat(void);

/**
 * @brief 一次性消费最新用户帧（后到覆盖先到；取走后清空）。
 * @return 有新帧且 cap 足够=true；否则 false（*len_out 不写）。
 */
bool Wireless_TakeLatestUser(uint8_t *buf, uint8_t cap, uint8_t *len_out);

/** @brief 有效帧总数（Link 活性刷新依据）。 */
uint32_t Wireless_RxFrameCount(void);

/** @brief 读诊断。NULL 安全。 */
void Wireless_GetDiag(Wireless_Diag_T *out);

#ifdef __cplusplus
}
#endif

#endif /* UART_WIRELESS_H */
