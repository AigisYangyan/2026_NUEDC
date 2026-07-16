/**
 * @file    emm42.h
 * @brief   EMM42 步进电机协议组包接口
 *
 * Driver 层只负责把参数编码成协议帧；总线队列编排由 stepmotor_bus
 * 提供兼容包装实现，维持现有上层调用名不变。
 */

#ifndef __EMM42_H__
#define __EMM42_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EMM42_UART_ID
#define EMM42_UART_ID 0u
#endif

#define EMM42_CMD_ENABLE        0xF3u
#define EMM42_CMD_SPEED         0xF6u
#define EMM42_CMD_POSITION      0xFDu
#define EMM42_CMD_PID_CFG       0x4Au
#define EMM42_CMD_READ_SPEED    0x35u

#define EMM42_SPEED_MIN_RPM        0u
#define EMM42_SPEED_MAX_RPM        100u
#define EMM42_SPEED_SCALE_X10      10u
#define EMM42_SPEED_MAX_PROTO      (EMM42_SPEED_MAX_RPM * EMM42_SPEED_SCALE_X10)
#define EMM42_ACCEL_MIN_GRADE      0u
#define EMM42_ACCEL_MAX_GRADE      255u

#define EMM42_MICROSTEP                256u
#define EMM42_PULSES_PER_REVOLUTION    51200u

#define EMM42_POSITION_MODE_RELATIVE   0u
#define EMM42_POSITION_MODE_ABSOLUTE   1u
#define EMM42_DIR_CW                   0x01u
#define EMM42_DIR_CCW                  0x00u
#define EMM42_POSITION_DIR_ABSOLUTE    EMM42_DIR_CCW
#define EMM42_POSITION_ACCEL_FIXED     0u
#define EMM42_POSITION_DEFAULT_RPM     30u

#define EMM42_ENABLE_ON     0x01u
#define EMM42_ENABLE_OFF    0x00u

#define EMM42_SYNC_FLAG     0x00u
#define EMM42_CHECK_BYTE    0x6Bu

typedef enum {
    EMM42_AXIS_Y = 1u,
    EMM42_AXIS_X = 2u
} Emm42_Axis_e;

bool Emm42_BuildEnableFrame(uint8_t axis_id,
                            uint8_t enable_status,
                            uint8_t *out,
                            uint8_t *out_len);
bool Emm42_BuildReadSpeedFrame(uint8_t axis_id,
                               uint8_t *out,
                               uint8_t *out_len);
bool Emm42_BuildSpeedFrame(uint8_t axis_id,
                           uint8_t direction,
                           uint16_t speed_rpm,
                           uint8_t acceleration,
                           uint8_t *out,
                           uint8_t *out_len);
bool Emm42_BuildPositionFrame(uint8_t axis_id,
                              uint8_t direction,
                              uint16_t speed_rpm,
                              uint8_t acceleration,
                              uint32_t pulses,
                              uint8_t mode,
                              uint8_t *out,
                              uint8_t *out_len);
bool Emm42_BuildSetZeroFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len);
bool Emm42_BuildStartHomingFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len);
bool Emm42_BuildExitHomingFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len);
bool Emm42_BuildPidConfigFrame(uint8_t axis_id,
                               uint8_t save_to_flash,
                               uint32_t kp,
                               uint32_t ki,
                               uint32_t kd,
                               uint8_t *out,
                               uint8_t *out_len);

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

#endif /* __EMM42_H__ */
