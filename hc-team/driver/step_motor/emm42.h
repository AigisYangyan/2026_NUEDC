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

/* 快速位置模式（手册 §5.3.13，"适用于二维云台"）：先用本帧预设一次速度/加速度/运动模式，
 * 之后每拍只发 Emm42_BuildQPosFrame 的 int32 脉冲。速度经 emm42 唯一限幅（≤100RPM）+ ×10 尺度。
 * mode = EMM42_POSITION_MODE_*（相对上一目标/绝对/相对当前）；组 8 字节帧。 */
bool Emm42_BuildQPosPresetFrame(uint8_t axis_id,
                                uint16_t speed_rpm,
                                uint8_t acceleration,
                                uint8_t mode,
                                uint8_t *out,
                                uint8_t *out_len);

/* 快速位置运动：仅一个有符号 int32 脉冲（大端），方向由符号承载（无独立 dir 字节）。组 7 字节帧。
 * 语义（相对/绝对）取决于最近一次 Emm42_BuildQPosPresetFrame 设定的 mode。 */
bool Emm42_BuildQPosFrame(uint8_t axis_id, int32_t pulses, uint8_t *out, uint8_t *out_len);

/* 多电机命令封装（手册 §5.3.1）：把调用方已拼好的子命令串（各子命令含自身地址与尾 0x6B）包进
 * [广播 0x00, 0xAA, 总长(2B,大端), <子命令串>, 0x6B]，总长 = sub_cmds_len + 5。一帧发完、同起。
 * sub_cmds_len ∈ [1, 26]，越界或 sub_cmds==NULL → 返回 false（护 out 缓冲）。本函数不解释子命令内容。 */
bool Emm42_BuildMultiCmdFrame(const uint8_t *sub_cmds,
                              uint8_t sub_cmds_len,
                              uint8_t *out,
                              uint8_t *out_len);

/* 将当前位置角度清零（手册 §5.2.3，功能码 0x0A 辅助码 0x6D）＝建立绝对坐标零点，供绝对位置模式参考。
 * 注意：与 Emm42_BuildSetZeroFrame(0x93 单圈回零零点) 语义不同。组 4 字节帧。 */
bool Emm42_BuildClearPositionFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __EMM42_H__ */
