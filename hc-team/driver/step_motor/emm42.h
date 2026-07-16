/**
 * @file    emm42.h
 * @brief   EMM42 步进电机驱动模块对外接口定义
 *
 * @details
 * 模块职责：
 * - 封装 EMM42 步进驱动器的使能、速度、位置与回零类控制命令。
 * - 为上层两轴步进控制逻辑提供统一的命令组包与调度接口。
 *
 * 设计约定：
 * 1. 当前模块面向双轴场景，轴 ID 固定为 Y=1、X=2
 * 2. 控制命令先在本模块组包，再交给共享串口运输层发送
 * 3. 位置运动同时支持相对/绝对模式，不在本模块内维护复杂状态机
 *
 * 单位与口径（Emm 固件）：
 * - `speed`：上层接口使用 RPM；发协议前内部统一换算为 0.1RPM（speed_tx = speed_rpm * 10）
 * - `acceleration`：加速度档位，范围 0~255；当前位置模式统一固定为 0，保证微动和定位口径一致
 * - `pulses`：脉冲数，正负由上层方向语义决定
 *
 * 依赖：
 * - 共享总线运输层已初始化，并提供管理帧/控制帧发送接口
 *
 * 使用方式：
 * 1. 先调用 Emm42_EnableAll()/Emm42_SendEnableCommand() 使能电机
 * 2. 再调用 Emm42_SendSpeedCommand()/Emm42_SendPositionCommand()/Emm42_MoveRelative() 下发控制命令
 * 3. 需要回零时调用 Emm42_SetZeroPosition()/Emm42_StartHoming()/Emm42_ExitHoming() 相关接口
 *
 * 注意：
 * - 本模块只负责协议组包与发送，不负责轨迹规划、闭环控制与位置反馈
 * - 若总线发送策略变化，应优先修改运输层，而非在本模块直接操作底层 UART
 */

#ifndef __EMM42_H__
#define __EMM42_H__

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

    /* ---- 模块宏定义 --------------------------------------------------------- */

    /* 保留旧的串口 ID 宏位，当前实际发送链路由共享总线接管。 */
#ifndef EMM42_UART_ID
#define EMM42_UART_ID 0u
#endif

/* 电机控制命令字。 */
#define EMM42_CMD_ENABLE        0xF3u
#define EMM42_CMD_SPEED         0xF6u
#define EMM42_CMD_POSITION      0xFDu
#define EMM42_CMD_PID_CFG       0x4Au
#define EMM42_CMD_READ_SPEED    0x35u /* 读取电机实时转速，返回帧 6 字节 */

/* Emm 固件速度/加速度口径。
 * - 上层 speed 统一使用 RPM，最大允许 100 RPM
 * - 协议字段使用 0.1RPM，因此 100 RPM -> 1000
 */
#define EMM42_SPEED_MIN_RPM        0u
#define EMM42_SPEED_MAX_RPM        100u
#define EMM42_SPEED_SCALE_X10      10u
#define EMM42_SPEED_MAX_PROTO      (EMM42_SPEED_MAX_RPM * EMM42_SPEED_SCALE_X10)
#define EMM42_ACCEL_MIN_GRADE      0u
#define EMM42_ACCEL_MAX_GRADE      255u

/* EMM42 机械口径约定：
 * - 电机整步一圈 = 200 step
 * - 当前驱动器细分配置 Mstep = 256
 * - 因此每圈脉冲数 = 200 * 256 = 51200 pulse/rev
 *
 * 注意：
 * - 当前代码发送的位置量 pulses，默认就是“按 256 细分换算后的原始脉冲数”
 * - 若驱动器细分改动，则该常量和所有位置换算都必须同步修改
 */
#define EMM42_MICROSTEP                256u
#define EMM42_PULSES_PER_REVOLUTION    51200u

/* EMM42 位置模式口径。 */
#define EMM42_POSITION_MODE_RELATIVE   0u
#define EMM42_POSITION_MODE_ABSOLUTE   1u
#define EMM42_POSITION_DIR_ABSOLUTE    EMM42_DIR_CCW
#define EMM42_POSITION_ACCEL_FIXED     0u
#define EMM42_POSITION_DEFAULT_RPM     30u

/* 电机转动方向定义。 */
#define EMM42_DIR_CW        0x01u
#define EMM42_DIR_CCW       0x00u

/* 使能状态定义。 */
#define EMM42_ENABLE_ON     0x01u
#define EMM42_ENABLE_OFF    0x00u

/* 通信协议固定字段。 */
#define EMM42_SYNC_FLAG     0x00u//协议同步字，固定为 0x00
#define EMM42_CHECK_BYTE    0x6Bu//协议校验字，固定为 0x6B

/* ---- 类型定义 ----------------------------------------------------------- */


    typedef enum {
        EMM42_AXIS_Y = 1u,
        EMM42_AXIS_X = 2u
    } Emm42_Axis_e;//电机轴枚举，当前仅支持 Y=1 和 X=2 两轴

    /* ---- 公开 API ----------------------------------------------------------- */

    /** 发送电机使能/失能命令。 */
    void Emm42_SendEnableCommand(uint8_t axis_id, uint8_t enable_status);

    /** 发送速度模式命令。
     *  @param speed 上层输入单位为 RPM；发协议前内部换算为 0.1RPM，范围 0~100
     *  @param acceleration Emm 固件加速度档位，范围 0~255
     */
    void Emm42_SendSpeedCommand(uint8_t axis_id,
        uint8_t direction,
        uint16_t speed,
        uint8_t acceleration);

    /** 发送位置模式命令。
     *  @param speed 上层输入单位为 RPM；发协议前内部换算为 0.1RPM，范围 0~100
     *  @param acceleration 上层参数保留兼容；当前实现固定按 0 发送，避免位置模式引入缓冲
     *  @param mod 0=相对位置，1=绝对位置
     */
    void Emm42_SendPositionCommand(uint8_t axis_id, uint8_t direction, uint16_t speed,
        uint8_t acceleration, uint32_t pulses, uint8_t mod);

    /** 使能全部电机轴。 */
    void Emm42_EnableAll(void);

    /** 关闭全部电机轴。 */
    void Emm42_DisableAll(void);

    /** 将全部电机轴当前位置设为原点。 */
    void Emm42_SetAllAxesZero(void);

    /** 发送单轴相对位移命令。
     *  @param speed 上层输入单位为 RPM；发协议前内部换算为 0.1RPM，范围 0~100
     *  @param acceleration Emm 固件加速度档位，范围 0~255
     */
    void Emm42_MoveRelative(Emm42_Axis_e axis,
        int32_t pulses,
        uint16_t speed,
        uint8_t acceleration);

    /** 发送单轴绝对位置命令。
     *  @brief 题目中做高精度定点定位时，优先使用这个接口，不要再手工拼 FD 帧。
     *  @param position_pulses 绝对位置脉冲数，按 256 细分口径（1 rev = 51200 pulse）
     *  @param speed 上层输入单位 RPM；发协议前内部换算为 0.1RPM，范围 0~100
     *  @note 推荐题目参数：V=30 RPM，A=0。
     *  @note 内部固定使用 DIR=00、ACC=00、MODE=01、SYNC=00。
     */
    void Emm42_MoveAbsolute(Emm42_Axis_e axis,
        uint32_t position_pulses,
        uint16_t speed);

    /** 双轴步进电机周期任务入口。 */
    void Emm42_RunCommandTask(void);

    /** 发送“当前位置设为原点”命令。 */
    void Emm42_SetZeroPosition(uint8_t axis_id);

    /** 发送“执行回原点”命令。 */
    void Emm42_StartHoming(uint8_t axis_id);

    /** 发送“退出回零模式”命令。 */
    void Emm42_ExitHoming(uint8_t axis_id);

    /** 发送 Emm 固件 PID 参数修改命令。 */
    void Emm42_SendPidConfigCommand(uint8_t axis_id,
        uint8_t save_to_flash,
        uint32_t kp,
        uint32_t ki,
        uint32_t kd);

    /** 发送“读取电机实时速度”查询命令（0x35）。
     *  驱动器收到后会回发一帧 6 字节：[Addr][0x35][Sign][SpdH][SpdL][0x6B]。
     *  解码由运输层 (stepmotor_bus) 完成，上层通过
     *  StepmotorBus_GetLastSpeedRpm() 读取最近一次反馈值。
     */
    void Emm42_SendReadSpeedCommand(uint8_t axis_id);

#ifdef __cplusplus
}
#endif

#endif /* __EMM42_H__ */
