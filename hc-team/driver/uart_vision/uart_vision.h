/**
 * @file    uart_vision.h
 * @brief   视觉链路协议编解码 Driver 对外接口
 *
 * 本模块坐落于 driver/board_uart/vision_uart 字节搬运层之上（Driver→Driver 同层受控），
 * 是 UART_VISION 上视觉协议的唯一编解码所有者。承担两条并行协议：
 *
 * - 控制/坐标帧（视觉→主控，RX，运行期）：
 *     0xAA 0x55 + 长度(1B) + payload + CRC16-MODBUS(2B 小端)
 *     payload = [cmd] + 数据；坐标帧 cmd=0x01 + x:float32 小端 + y:float32 小端（len=9）
 *     CRC 范围 = 长度字节 + payload
 * - 选题/握手帧（双向，setup 期）：
 *     0xFF + 主任务号(1B) + 子任务号(1B) + 0xFE（无校验，定长 4B）
 *     主控发选题；视觉同格式回一帧确认。
 *
 * 设计约定：
 * - 自同步分帧（长度前缀 + CRC16 + len 白名单）→ 确定性自恢复，不设逐字节超时、不依赖 Clock。
 * - 坐标原样透传（float32，视觉坐标系）；坐标→轴映射不在本层（归 Middleware）。
 * - 时效判定归上层：本层只给单调递增 seq，上层消费 seq 变化判「是否有新帧」。
 * - 运行期只解析坐标帧；校验通过但未知 cmd 的帧静默丢弃（不预建 0x02 目标状态处理）。
 */
#ifndef UART_VISION_H
#define UART_VISION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 视觉像素坐标，float32，视觉坐标系原样透传（不做单位/极性变换）。 */
typedef struct {
    float x;
    float y;
} UartVision_Coord_T;

/** 清编解码状态并初始化底层字节层；不发送任何帧。 */
void UartVision_Init(void);

/** 任务态推进：drain VisionUart_Read → 自同步分帧 → 刷新坐标/确认缓存。 */
void UartVision_Poll(void);

/* ---- 坐标控制帧（RX，运行期） ---------------------------------------------- */

/**
 * 读取最近一次有效目标坐标。
 * @param out 输出坐标（float32）。
 * @return 收到过坐标帧且 out 非空→true 并写出；否则 false。
 */
bool UartVision_GetLatestCoord(UartVision_Coord_T *out);

/** 坐标单调递增序号（每成功解析一坐标帧 +1，初值 0）；上层比较增量判时效。 */
uint32_t UartVision_GetCoordSeq(void);

/* ---- 选题/握手帧（setup 期） ----------------------------------------------- */

/**
 * 组 0xFF 选题帧经 VisionUart_TryWrite 下发。
 * @param main_task 主任务号。
 * @param sub_task  子任务号。
 * @return 成功提交→true；底层 TX 忙→false（不发、不排队）。
 */
bool UartVision_SendTopic(uint8_t main_task, uint8_t sub_task);

/**
 * 读取最近一次视觉回传的选题确认帧。
 * @param main_task 输出确认回显的主任务号（可为 NULL）。
 * @param sub_task  输出确认回显的子任务号（可为 NULL）。
 * @return 收到过确认帧→true；否则 false。
 */
bool UartVision_GetTopicAck(uint8_t *main_task, uint8_t *sub_task);

/** 确认帧单调递增序号（每收一确认帧 +1，初值 0）；上层判「本轮选题是否已确认」。 */
uint32_t UartVision_GetTopicAckSeq(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_VISION_H */
