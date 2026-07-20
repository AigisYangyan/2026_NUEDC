/**
 * @file    gimbal_stepbus.h
 * @brief   云台服务内私有：最小步进 TX 派发（Service→Driver 直连，契约 §21.3）
 *
 * 本模块坐落 driver/step_motor/emm42（协议组包）+ driver/board_uart/stepmotor_uart（字节层）
 * 之上，是 gimbal 服务把「双轴当前绝对脉冲目标」翻译成一帧 0xAA 多电机命令（内含两条 FC 快速位置
 * 子命令）并发上 UART7 的最小派发点。两轴同帧、同起（手册 §5.3.1）。**不复刻** 冻结的
 * app/tasks/platform_2d/stepmotor_bus.c 的 mgmt 队列 / RR 仲裁 / 0x35 读速度应答——瞄准环只需
 * 「总线空就发一帧」。
 *
 * 单一所有者（数据链 §8.2）：
 * - RPM 限幅 ≤100 + ×10 协议尺度不在本层，唯一所有者是 emm42.c（本层传裸 speed_rpm）；
 * - 0xAA 封装 / FC 编码不在本层，唯一所有者是 emm42.c（本层只按轴序拼子命令串）；
 * - 字节/DMA 搬运不在本层，唯一所有者是 stepmotor_uart。
 * - **符号即方向**：FC 帧的目标为有符号 int32，方向由符号承载——绝对方案下无「脉冲→dir+幅值」
 *   拆分级（旧相对方案的拆分所有者已随本次改造移除）。极性唯一所有者仍是 vision_aim.sign。
 *
 * 本模块不拥有：轴累计绝对位置状态（归 gimbal.c）、时效判定（归 gimbal.c）、控制编排（归 gimbal.c）。
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
 * 总线空 → 把双轴当前绝对脉冲目标拼成一帧 0xAA（内含 FC_Y(y_pulse) ∥ FC_X(x_pulse)，Y=器件 addr1
 * 在前）→ StepmotorUart_TryWrite 发出，返回 true；总线忙 → false（不发、不排队）。
 * 目标为有符号 int32 绝对值（符号即方向）；绝对语义依赖 ARMING 期 TrySendPreset 设定的 mode=ABSOLUTE。
 * 恒发双轴（即便某轴目标未变）——绝对重发幂等，保帧长恒定与心跳节奏。
 */
bool GimbalStepbus_TrySendDualAbsolute(int32_t x_pulse, int32_t y_pulse);

/** 总线空 → 组 enable(on/off) 帧发出，true；忙 → false。 */
bool GimbalStepbus_TrySendEnable(GimbalStepbus_Axis axis, bool on);

/**
 * 总线空 → 组 F1 快速位置预设帧（速度=speed_rpm 交 emm42 限幅；内部固定 mode=ABSOLUTE、加速度=0）
 * 发出，true；忙 → false。ARMING 期每轴设定一次；之后 TrySendDualAbsolute 只发脉冲。
 */
bool GimbalStepbus_TrySendPreset(GimbalStepbus_Axis axis, uint16_t speed_rpm);

/**
 * 总线空 → 组 0x0A「清当前位置」帧（建立绝对坐标零点，供绝对位置模式参考）发出，true；忙 → false。
 * 注意：非 0x93 单圈回零零点——绝对方案的原点须用清当前位置。
 */
bool GimbalStepbus_TrySendClearZero(GimbalStepbus_Axis axis);

#ifdef __cplusplus
}
#endif

#endif /* GIMBAL_STEPBUS_H */
