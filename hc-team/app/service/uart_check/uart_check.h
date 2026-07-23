/**
 * @file    uart_check.h
 * @brief   串口链路诊断服务：五条 UART 链路的字节层丢失计数一页可查（UDIAG §39）。
 *
 * 能力辩护（§3.4）：「板子能报每条串口链路丢没丢字节」——丢字节是一切"丢帧悬案"
 * 的字节层母因（docs/Final/通信数据包与缓冲区方案.md §1 三层模型的第一层出口）。
 * 任何一轮 VOFA 调参/联调跑完，进 UartDiag 条目即知有没有丢、丢在哪条链。
 *
 * 所有权（§8.2）：溢出计数的唯一所有者是各 board_uart 端口；本服务只做只读镜像，
 * 零第二数据变换（imu_check 先例）。imu 计数直读 ImuUart_GetRxOverflowCount
 * （端口层），不经 driver/imu 的 Imu_Diag——同源只读双读者合法，不新增泵点。
 * 计数皆 Driver 累计型：跨进退页保留（证据不因换页蒸发）。
 */
#ifndef UART_CHECK_H
#define UART_CHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 一次读取到的诊断遥测（与 VOFA 通道 ch0..ch5 同源同序）。 */
typedef struct {
    uint32_t vofa_rx_ovf;    /**< VOFA 命令 RX 丢字节（调参命令没反应先看它）。 */
    uint32_t vofa_tx_ovf;    /**< VOFA 遥测 TX 环满拒帧（波形断点的 MCU 侧母因）。 */
    uint32_t vision_rx_ovf;  /**< 视觉链路 RX 丢字节。 */
    uint32_t step_rx_ovf;    /**< 步进总线 RX 丢字节。 */
    uint32_t imu_rx_ovf;     /**< IMU 链路 RX 丢字节。 */
    uint32_t wl_rx_ovf;      /**< 无线端口 RX 丢字节（占位端口恒 0）。 */
} UartCheck_Telemetry_T;

/** @brief 进入诊断会话：清 VOFA 表 + 注册 tx×6。只读，无任何硬件副作用。 */
void UartCheck_Start(void);

/**
 * @brief 周期推进：10ms 自门控——读六计数 → 镜像 → vofa_run 发帧。
 * @param now_ms 调度器注入的当前毫秒时刻（无符号减法天然处理回绕）。
 * @note  首拍只播种周期基准；未 Start 时调用无副作用。
 */
void UartCheck_Update(uint32_t now_ms);

/** @brief 退出诊断会话：清 VOFA 表。计数留在 Driver 不清零。 */
void UartCheck_Stop(void);

/** @brief 读取最近一次到期拍的遥测。NULL 安全。 */
void UartCheck_GetTelemetry(UartCheck_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* UART_CHECK_H */
