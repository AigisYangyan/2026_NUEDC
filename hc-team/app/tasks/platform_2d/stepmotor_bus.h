/**
 * @file    stepmotor_bus.h
 * @brief   步进电机专用串口总线服务模块对外接口定义
 *
 * 本模块负责 UART_STEPPER_BUS (UART7, 230400) 上的步进电机收发调度。
 * 原始字节由 StepmotorUart Driver 缓存；本模块在任务态解析应答并编排队列发送。
 */

#ifndef STEPMOTOR_BUS_H
#define STEPMOTOR_BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STEPMOTOR_BUS_SHARED_RX_FIFO_SIZE 256u

typedef enum {
    STEPMOTOR_BUS_OK = 0,
    STEPMOTOR_BUS_ERR_INVALID = -2,
    STEPMOTOR_BUS_ERR_NOT_READY = -10,
    STEPMOTOR_BUS_ERR_BUSY = -11,
} StepmotorBus_Status_e;

void StepmotorBus_Init(void);
void StepmotorBus_Service5ms(void);
bool StepmotorBus_RequestBypass(void);
void StepmotorBus_SetBypass(bool bypass);
void StepmotorBus_SetControlGate(bool enable);
void StepmotorBus_ResetDiagCounters(void);
uint32_t StepmotorBus_GetControlErrorCount(void);
uint8_t StepmotorBus_GetLastReturnCode(void);
void StepmotorBus_ClearControlFrames(void);
bool StepmotorBus_IsControlPathIdle(void);
int32_t StepmotorBus_GetLastSpeedRpm(uint8_t axis_id);
int32_t StepmotorBus_GetLastSpeedRaw(uint8_t axis_id);

#ifdef __cplusplus
}
#endif

#endif /* STEPMOTOR_BUS_H */
