/**
 * @file    uart_stress.h
 * @brief   DEBUG UART_Stress 230400/5ms 压测模块对外接口
 *
 * 本模块用于验证 UART_STEPPER_BUS 在 230400 波特率下、
 * 5ms 周期双向收发的底层链路稳定性，不依赖电机/视觉业务代码。
 *
 * 功能范围：
 * - 5ms 周期主动发送 8 字节模拟电机控制帧
 * - 同步接收上位机回传字节，按 AA55 + checksum 滑窗解帧后再 echo
 * - 维护心跳/字节计数/错误计数供调试观察
 * - 通过 PB22 LED 闪烁指示任务仍在运行
 *
 * 设计约定：
 * - 进入运行项时暂停 StepmotorBus 消费并接管 RX 回调
 * - 退出运行项时恢复 StepmotorBus 回调并清空计数
 * - 不修改 UART/DMA 配置，波特率由 SysConfig 决定
 */

#ifndef __UART_STRESS_H__
#define __UART_STRESS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 公开 API ----------------------------------------------------------- */

/** 初始化 UART_Stress 模块本地状态，清零计数。 */
void UartStress_Init(void);

/** 进入 UART_Stress 运行项：挂 RX 钩子并暂停 StepmotorBus。 */
void UartStress_Enter(void);

/** 退出 UART_Stress 运行项：恢复 StepmotorBus RX 回调。 */
void UartStress_Exit(void);

/** 5ms 周期入口：主动发 8B + 滑窗解帧 RX + echo + 心跳自增。 */
void UartStress_Tick5ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_STRESS_H__ */
