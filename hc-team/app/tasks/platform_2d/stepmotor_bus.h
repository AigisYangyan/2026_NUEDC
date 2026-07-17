/**
 * @file    stepmotor_bus.h
 * @brief   步进电机专用串口总线服务模块对外接口定义
 *
 * 本模块负责 UART_STEPPER_BUS (UART7, 230400) 上的步进电机收发调度。
 * 原始字节由 StepmotorUart Driver 缓存；本模块在任务态解析应答并编排队列发送。
 */

#ifndef STEPMOTOR_BUS_H
#define STEPMOTOR_BUS_H

#include "driver/step_motor/emm42.h"  /* Emm42_Axis_e：轴号是器件协议事实 */

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

/* ---- 兼容包装层：以 Emm42_* 命名的总线动作 -------------------------------
 * 下列函数**实现在本模块**（stepmotor_bus.c），不是 Driver 提供的能力：
 * 它们在 emm42.c 组好的协议帧之上，叠加了本模块的队列编排、控制门与应答处理。
 *
 * 沿用 Emm42_* 前缀是 P5 的历史包袱（当时为维持上层调用名不变）。名字属 App，
 * 归属也属 App —— 2026-07-17（P9.T2 / V18）把声明从 driver/step_motor/emm42.h
 * 迁到这里，Driver 头不再宣称提供它们。
 *
 * 上层重置时这批名字应随本模块一起重命名为 StepmotorBus_*。
 * ------------------------------------------------------------------------ */
void Emm42_SendEnableCommand(uint8_t axis_id, uint8_t enable_status);
void Emm42_SendSpeedCommand(uint8_t axis_id,
                            uint8_t direction,
                            uint16_t speed,
                            uint8_t acceleration);
void Emm42_SendPositionCommand(uint8_t axis_id,
                               uint8_t direction,
                               uint16_t speed,
                               uint8_t acceleration,
                               uint32_t pulses,
                               uint8_t mod);
void Emm42_EnableAll(void);
void Emm42_DisableAll(void);
void Emm42_SetAllAxesZero(void);
void Emm42_MoveRelative(Emm42_Axis_e axis,
                        int32_t pulses,
                        uint16_t speed,
                        uint8_t acceleration);
void Emm42_MoveAbsolute(Emm42_Axis_e axis,
                        uint32_t position_pulses,
                        uint16_t speed);
void Emm42_SetZeroPosition(uint8_t axis_id);
void Emm42_StartHoming(uint8_t axis_id);
void Emm42_ExitHoming(uint8_t axis_id);
void Emm42_SendPidConfigCommand(uint8_t axis_id,
                                uint8_t save_to_flash,
                                uint32_t kp,
                                uint32_t ki,
                                uint32_t kd);
void Emm42_SendReadSpeedCommand(uint8_t axis_id);

#ifdef __cplusplus
}
#endif

#endif /* STEPMOTOR_BUS_H */
