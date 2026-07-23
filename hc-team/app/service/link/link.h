/**
 * @file    link.h
 * @brief   无线链路服务：心跳/重试节拍 + 活性窗口（能力：链路活着吗、状态能发/收、
 *          事件必达或明确失败）。
 *
 * WL2 契约 §38：codec（uart_wireless）管 what（帧/seq/去重/ACK），本服务管 when——
 * 心跳 200ms、活性 600ms 窗口、事件重传 40ms(4 tick)×上限 8 次后放弃。
 * TX 梯队：ACK（codec Poll 内即回）＞事件重发＞状态＞心跳；周期内已有成功数据 TX
 * 则该拍心跳被抑制（对端活性已被刷新，重复心跳浪费带宽）。
 * **双停等安全政策不在本服务**——消费者（T01/route）凭 Link_IsAlive 自行决策。
 * LinkTest 遥测（VOFA tx×10）经 Link_StartTelemetry/StopTelemetry 条目作用域开关。
 */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     alive;
    uint32_t rx_frames;
    uint32_t crc_errors;
    uint32_t ur_gap;         /**< 不可靠流测得丢帧数（镜像 diag.ur_gap_count）。 */
    uint32_t retx;           /**< 事件重传次数。 */
    uint32_t delivered;      /**< 事件被 ACK 确认数。 */
    uint32_t ev_fail;        /**< 事件重试耗尽放弃数。 */
    uint32_t hb_sent;        /**< 成功写入端口的心跳帧数（端口缺席恒 0）。 */
    uint32_t rx_overflows;
    bool     port_absent;
} Link_Telemetry_T;

/** @brief Wireless_Init + 节拍/活性/重试状态复位。 */
void Link_Init(void);

/** @brief 10ms 自门控：Poll → 活性刷新 → 事件重试节拍 → 心跳（→遥测发帧若开）。 */
void Link_Update(uint32_t now_ms);

/** @brief 最近一次 Update 判定的活性（600ms 窗口；从未收到帧恒 false）。 */
bool Link_IsAlive(void);

/** @brief 发送状态帧（不可靠，最新胜出；对端可测丢包率）。 */
bool Link_SendState(const uint8_t *data, uint8_t len);

/** @brief 一次性消费对端最新状态帧。 */
bool Link_TakeState(uint8_t *buf, uint8_t cap, uint8_t *len_out);

/** @brief 发起必达事件（单在途 stop-and-wait）。在途未决→false，稍后重试。 */
bool Link_SendEvent(const uint8_t *data, uint8_t len);

/** @brief 是否有在途未确认事件（false 后看遥测 delivered/ev_fail 分辨结局）。 */
bool Link_EventBusy(void);

/** @brief 出队对端事件（FIFO 全收）。 */
bool Link_TakeEvent(uint8_t *buf, uint8_t cap, uint8_t *len_out);

/** @brief 读遥测快照。NULL 安全。 */
void Link_GetTelemetry(Link_Telemetry_T *out);

/** @brief 注册 VOFA tx×10（alive/rx/crc/gap/retx/dlvd/fail/hb/ovf/absent），LinkTest 进页调。 */
void Link_StartTelemetry(void);

/** @brief 清 VOFA 表，LinkTest 退页调。 */
void Link_StopTelemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* LINK_H */
