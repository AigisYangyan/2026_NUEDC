/**
 * @file    link.h
 * @brief   无线链路服务：心跳节拍 + 活性窗口（能力：链路活着吗、能发/收一包）。
 *
 * WL1 契约 §35：心跳 200ms 周期 TX；活性=600ms 窗口内收到过任意有效帧（用户或
 * 对端心跳）。**双停等安全政策不在本服务**——消费者（T01/route）凭 Link_IsAlive
 * 自行决策；本服务只提供链路事实（预测文档 P3「心跳断→双停」的事实半边）。
 * LinkTest 遥测（VOFA tx×6）经 Link_StartTelemetry/StopTelemetry 条目作用域开关。
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
    uint32_t hb_sent;        /**< 成功写入端口的心跳帧数（端口缺席恒 0）。 */
    uint32_t rx_overflows;
    bool     port_absent;
} Link_Telemetry_T;

/** @brief Wireless_Init + 节拍/活性状态复位。 */
void Link_Init(void);

/** @brief 10ms 自门控：Wireless_Poll → 活性刷新 → 200ms 心跳 TX（→遥测发帧若开）。 */
void Link_Update(uint32_t now_ms);

/** @brief 最近一次 Update 判定的活性（600ms 窗口；从未收到帧恒 false）。 */
bool Link_IsAlive(void);

/** @brief 发送用户数据帧（透传对端）。 */
bool Link_Send(const uint8_t *data, uint8_t len);

/** @brief 一次性消费最新对端用户帧。 */
bool Link_TakeLatest(uint8_t *buf, uint8_t cap, uint8_t *len_out);

/** @brief 读遥测快照。NULL 安全。 */
void Link_GetTelemetry(Link_Telemetry_T *out);

/** @brief 注册 VOFA tx×6（alive/rx/crc/hb/ovf/port_absent），LinkTest 进页调。 */
void Link_StartTelemetry(void);

/** @brief 清 VOFA 表，LinkTest 退页调。 */
void Link_StopTelemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* LINK_H */
