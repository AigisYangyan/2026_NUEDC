/**
 * @file    vision.h
 * @brief   视觉服务（车用）：选题握手编排 + 最新坐标/识别状态读取面（V1 契约 §36）。
 *
 * 握手编排从已废弃 gimbal 服务重建（零步进内容）：SelectTopic 发起后，未确认期
 * 500ms 自动重发，视觉回显题号一致即 confirmed（换题重置）。坐标/状态原样透传
 * uart_vision（零第二处理，§8.2）；状态位域语义归 T01 消费者。
 * VisionLink 遥测（VOFA tx×8）经 Start/StopTelemetry 条目作用域开关。
 */
#ifndef VISION_H
#define VISION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UartVision_Init + 握手/遥测状态复位。不发送任何帧。 */
void Vision_Init(void);

/**
 * @brief 发起选题：立即发一帧并登记；未确认期由 Vision_Update 每 500ms 重发。
 * @return 首帧是否成功写入端口（false 时重发节拍照常兜底）。
 */
bool Vision_SelectTopic(uint8_t main_task, uint8_t sub_task);

/** @brief 视觉回显与所选题号一致后恒 true；换题（再次 SelectTopic）重置。 */
bool Vision_IsTopicConfirmed(void);

/** @brief 10ms 自门控：UartVision_Poll → 确认跟踪 → 重发节拍 →（遥测发帧若开）。 */
void Vision_Update(uint32_t now_ms);

/** @brief 最新坐标（float32 视觉坐标系原样）。未收到过→false。 */
bool Vision_GetLatestCoord(float *x, float *y);

/** @brief 坐标单调 seq（透传 uart_vision）。 */
uint32_t Vision_CoordSeq(void);

/** @brief 最新目标状态（2×位域字节原样）。未收到过→false。 */
bool Vision_GetLatestStatus(uint8_t out[2]);

/** @brief 状态单调 seq（透传 uart_vision）。 */
uint32_t Vision_StatusSeq(void);

/** @brief 注册 VOFA tx×8（x/y/coord_seq/st0/st1/st_seq/confirmed/retries）。 */
void Vision_StartTelemetry(void);

/** @brief 清 VOFA 表。 */
void Vision_StopTelemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_H */
