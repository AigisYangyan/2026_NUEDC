/**
 * @file    emm42.h
 * @brief   EMM42 步进电机协议组包接口
 *
 * 模块职责：
 * - 把参数编码成 EMM42 协议帧（纯组包，零副作用）
 *
 * 本 Driver **不负责**：
 * - 不碰串口，不发送。组好的帧交给调用方，由 App 的 stepmotor_bus 排队发出
 * - 不做总线队列编排、不等应答、不重试
 *
 * ★ 2026-07-17（P9.T2 / 违规 V18）：本头曾声明 13 个总线动作函数（发送、移动、
 *   回零一类），而它们实现在 App 层 app/tasks/platform_2d/stepmotor_bus.c。
 *   那让 Driver 头对外宣称 Driver 提供这些能力，实则不提供 —— 单独链接 emm42.o
 *   会得到未定义引用。现已迁往 stepmotor_bus.h（实现所在层）。
 *   **本头此后只允许声明 emm42.c 自己实现的符号。**
 */

#ifndef __EMM42_H__
#define __EMM42_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
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

#ifdef __cplusplus
}
#endif

#endif /* __EMM42_H__ */
