/**
 * @file    uart_stress.h
 * @brief   DEBUG UART_Stress 230400/5ms 压测模块对外接口
 *
 * 本模块通过 StepmotorBus 的暂停/恢复接口独占 UART_STEPPER_BUS，
 * 并在任务态经 StepmotorUart 读写测试帧，不再替换 runtime 回调。
 */

#ifndef __UART_STRESS_H__
#define __UART_STRESS_H__

#ifdef __cplusplus
extern "C" {
#endif

void UartStress_Init(void);
void UartStress_Enter(void);
void UartStress_Exit(void);
void UartStress_Tick5ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_STRESS_H__ */
