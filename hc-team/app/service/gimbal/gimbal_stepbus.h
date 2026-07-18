/**
 * @file    gimbal_stepbus.h
 * @brief   云台服务内私有：最小步进 TX 派发（Service→Driver 直连，契约 §21.3）
 *
 * 本模块坐落 driver/step_motor/emm42（协议组包）+ driver/board_uart/stepmotor_uart（字节层）
 * 之上，是 gimbal 服务把「往哪个轴走多少脉冲」翻译成一帧 EMM42 相对位置命令并发上 UART7 的
 * 最小派发点。**不复刻** 冻结的 app/tasks/platform_2d/stepmotor_bus.c 的 mgmt 队列 / RR 仲裁 /
 * 0x35 读速度应答——瞄准环只需「总线空就发一帧」。
 *
 * 单一所有者（数据链 §8.2）：
 * - 脉冲 → 方向(dir)+幅值(magnitude) 拆分只在本模块（承 S05b §21.2 边界）；
 * - RPM 限幅 ≤100 + ×10 协议尺度不在本层，唯一所有者是 emm42.c（本层传裸 speed_rpm）；
 * - 字节/DMA 搬运不在本层，唯一所有者是 stepmotor_uart。
 *
 * 本模块不拥有：轴累计位置状态（归 gimbal.c）、时效判定（归 gimbal.c）、控制编排（归 gimbal.c）。
 */
#ifndef GIMBAL_STEPBUS_H
#define GIMBAL_STEPBUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 云台双轴（与 vision_aim 轴序一致：X=0/Y=1）；轴号→emm42 器件 id 在本模块内部转换。 */
typedef enum {
    GIMBAL_STEPBUS_AXIS_X = 0,
    GIMBAL_STEPBUS_AXIS_Y,
    GIMBAL_STEPBUS_AXIS_COUNT
} GimbalStepbus_Axis;

/** 清私有状态；底层 StepmotorUart_Init 由 system 装配（此处不重复初始化硬件）。 */
void GimbalStepbus_Init(void);

/**
 * 每次服务：消费 TX 完成事件（ConsumeTxDone）+ drain/discard 步进 RX（界定 FIFO，不解析——
 * 步进应答有意不用，视觉是唯一反馈路径）。
 */
void GimbalStepbus_Service(void);

/** 总线可发：底层 TX 空闲（未忙）。 */
bool GimbalStepbus_IsIdle(void);

/**
 * 总线空且 pulses≠0 → 拆 dir(≥0→CW / <0→CCW)+magnitude(|pulses|) → 组 EMM42 相对位置帧 →
 * StepmotorUart_TryWrite 发出，返回 true；总线忙 / pulses==0 → false（不发、不排队）。
 * @param speed_rpm 交 emm42 限幅（≤100 + ×10），本层不夹。
 */
bool GimbalStepbus_TrySendRelative(GimbalStepbus_Axis axis, int32_t pulses, uint16_t speed_rpm);

/** 总线空 → 组 enable(on/off) 帧发出，true；忙 → false。 */
bool GimbalStepbus_TrySendEnable(GimbalStepbus_Axis axis, bool on);

/** 总线空 → 组 set-zero（置当前位置为原点）帧发出，true；忙 → false。 */
bool GimbalStepbus_TrySendSetZero(GimbalStepbus_Axis axis);

#ifdef __cplusplus
}
#endif

#endif /* GIMBAL_STEPBUS_H */
