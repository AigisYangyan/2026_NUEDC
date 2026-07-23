/**
 * @file    uart_wireless.h
 * @brief   无线链路 Driver：MCU↔ESP32-C3 UART 帧编解码 v2 + 双投递语义 + 诊断。
 *
 * 协议 v2（WL2 契约 §38.1，对称双向；对 §35.1 的显式修订——加 seq 字节）：
 *   0xA5 0x5A | len(1B=payload长) | type(1B) | seq(1B) | payload(≤32B) | CRC16-MODBUS(2B 小端)
 *   CRC 范围 = len+type+seq+payload。
 *
 * 两级投递语义（「每帧不丢」的工程分解）：
 * - 不可靠流：type 0x01=STATE（最新帧信箱，后到覆盖）+ 0x02=心跳（空载）。二者共用
 *   一个 TX seq 计数器；接收侧按 seq 记 gap（丢包量）与 dup（重复排除）——丢包率
 *   随时可查，但不重传（状态类过期即无价值）。
 * - 可靠流：type 0x03=EVENT（stop-and-wait 单在途 pending 槽 + 0x04=ACK + 同 seq
 *   重传 + 收端去重&深 4 队列全收）——必达或明确失败可查。收端队列满时**不 ACK**
 *   （宁可让对端重传，不假确认后丢弃）。
 *
 * 本层刻意不做的事（Q2/Q7 分层先例）：无时间轴、无心跳/重试节拍、无活性判定——
 * 全归 app/service/link（codec 管 what，link 管 when）。ACK 是唯一例外的"主动 TX"：
 * 在 Poll 解析内即收即回（反应式，不需要时间）。ESP-NOW 组网对本层透明。
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
    uint32_t frame_count;        /**< CRC 通过且 type 已知的帧总数（含 dup，活性依据）。 */
    uint32_t crc_error_count;    /**< CRC 不符或 len 越界被拒的帧数。 */
    uint32_t unknown_type_count; /**< CRC 通过但 type 未知的拒收帧数（协议失配探针）。 */
    uint32_t ur_gap_count;       /**< 不可靠流 seq 空洞累计（=测得的丢帧数）。 */
    uint32_t ur_dup_count;       /**< 不可靠流重复 seq 排除数。 */
    uint32_t ev_rx_drop_count;   /**< 事件队列满丢弃数（未 ACK，对端会重传）。 */
    uint32_t ev_dup_count;       /**< 重复事件（重 ACK 不重投）数。 */
    uint32_t retx_count;         /**< 本端事件重传次数。 */
    uint32_t delivered_count;    /**< 本端事件被 ACK 确认数。 */
    uint32_t ev_fail_count;      /**< 本端事件重试耗尽放弃数。 */
    uint32_t tx_fail_count;      /**< 端口 Write 拒绝累计（TX 侧字节层背压探针）。 */
    uint32_t rx_overflows;       /**< 端口 RX 溢出（镜像 WirelessUart_GetRxOverflowCount）。 */
    bool     port_absent;        /**< true=端口占位/初始化失败（引脚未定案期恒 true）。 */
} Wireless_Diag_T;

/** @brief 解析器/信箱/队列/seq/诊断复位 + 端口初始化（结果记入 port_absent）。 */
void Wireless_Init(void);

/** @brief 排空端口 RX 并解析（自同步分帧；EVENT 即收即回 ACK）。任务态周期调用。 */
void Wireless_Poll(void);

/** @brief 组帧发送状态帧（0x01，不可靠流 seq++）。len>32 或端口缺席→false。 */
bool Wireless_SendState(const uint8_t *data, uint8_t len);

/** @brief 组帧发送心跳（0x02，空载，与 STATE 共用 seq）。端口缺席→false。 */
bool Wireless_SendHeartbeat(void);

/**
 * @brief 发起可靠事件（0x03）：存入 pending 槽（消费一个事件 seq）并首发。
 * @return 已有在途事件 / len>32 / 端口缺席 → false（未消费 seq）。
 * @note  首发的端口层失败不算发起失败（重传机制会兜住，tx_fail_count 留痕）。
 */
bool Wireless_SendEvent(const uint8_t *data, uint8_t len);

/** @brief 以同 seq 重发 pending 事件（重试节拍归 link）。无在途→false。 */
bool Wireless_ResendEvent(void);

/** @brief 放弃 pending 事件（ev_fail_count++）。无在途则无副作用。 */
void Wireless_AbandonEvent(void);

/** @brief 是否有未被 ACK 的在途事件（ACK 到达即自动清）。 */
bool Wireless_EventPending(void);

/** @brief 一次性消费最新状态帧（后到覆盖先到；cap 不足则保留返回 false）。 */
bool Wireless_TakeLatestState(uint8_t *buf, uint8_t cap, uint8_t *len_out);

/** @brief 出队最老的对端事件（FIFO 全收；空或 cap 不足→false 且保留）。 */
bool Wireless_TakeEvent(uint8_t *buf, uint8_t cap, uint8_t *len_out);

/** @brief 有效帧总数（Link 活性刷新依据）。 */
uint32_t Wireless_RxFrameCount(void);

/** @brief 读诊断。NULL 安全。 */
void Wireless_GetDiag(Wireless_Diag_T *out);

#ifdef __cplusplus
}
#endif

#endif /* UART_WIRELESS_H */
