/**
 * @file    emm42.c
 * @brief   EMM42 步进电机驱动模块实现
 *
 * 本文件实现 EMM42 步进驱动器的命令组包与发送逻辑，作为两轴步进控制的执行层。
 *
 * 功能范围：
 * - 组包并发送使能、速度、位置与回零相关指令
 * - 提供双轴统一使能/失能/置零接口
 * - 提供单轴相对位移接口与兼容旧逻辑的任务入口
 *
 * 不负责的内容：
 * - 轨迹规划、加减速曲线生成
 * - 编码器闭环、位置反馈与误差校正
 * - 底层串口发送节拍与仲裁策略
 *
 * 实现说明：
 * 1. 管理类指令与控制类指令分别走共享运输层的两条发送入口
 * 2. 当前位置指令同时支持相对模式与绝对模式
 * 3. 模块内只做协议字段组装，不保存复杂运行态
 *
 * 硬件绑定项：
 * - 双轴 EMM42 驱动器地址（Y=1, X=2）
 * - 共享串口运输层发送接口
 *
 * 若后续切换其他步进驱动器协议，应优先替换本文件的组包逻辑。
 */

#include <stdint.h>
#include "emm42.h"

/* ---- 模块宏定义 --------------------------------------------------------- */

/* 保留现有应用逻辑中的电机 ID 映射。 */
#define EMM42_AXIS_ID_Y               ((uint8_t)EMM42_AXIS_Y)
#define EMM42_AXIS_ID_X               ((uint8_t)EMM42_AXIS_X)

/* 回零相关命令字与固定负载。 */
#define EMM42_CMD_ORIGIN_SET          0x93u
#define EMM42_CMD_ORIGIN_RUN          0x9Au
#define EMM42_CMD_ORIGIN_QUIT         0x9Cu
#define EMM42_CMD_PID_CFG_AUX         0xC3u
#define EMM42_ORIGIN_SET_FIXED_0      0x88u
#define EMM42_ORIGIN_SET_FIXED_1      0x01u
#define EMM42_ORIGIN_RUN_FIXED_0      0x00u
#define EMM42_ORIGIN_RUN_FIXED_1      0x00u
#define EMM42_ORIGIN_QUIT_FIXED_0     0x48u

/* ---- 模块状态 ----------------------------------------------------------- */

/* 保留原有默认控制参数，供调试或旧逻辑恢复时复用。
 * Emm 固件口径：
 * - 上层 speed：RPM，0~100
 * - 协议 speed 字段：0.1RPM，上层发送前统一乘 10
 * - acceleration：速度模式保留 0~255；位置模式固定按 0 发送
 */
int g_emm42_default_acceleration = 0;
int g_emm42_default_speed = 30;

/* Transport returns StepmotorBus_Status_e values as int (0 = OK). */
extern int Emm42_TransportSendMgmtFrame(const uint8_t *frame, uint8_t len);
extern int Emm42_TransportSubmitControlFrame(uint8_t axis_id,
                                             const uint8_t *frame,
                                             uint8_t len);

/* ---- 静态辅助函数 ------------------------------------------------------- */

/* 通过共享串口运输层发送一整帧管理类 EMM42 指令。 */
static void emm42_send_mgmt_frame(const uint8_t *frame, uint8_t length)
{
    if ((frame == 0) || (length == 0u)) {
        return;
    }

    (void)Emm42_TransportSendMgmtFrame(frame, length);
}

/* 将最新控制命令提交给共享总线，旧的未发控制帧会被覆盖。 */
static void emm42_submit_control_frame(uint8_t axis_id,
                                       const uint8_t *frame,
                                       uint8_t length)
{
    if ((frame == 0) || (length == 0u)) {
        return;
    }

    (void)Emm42_TransportSubmitControlFrame(axis_id, frame, length);
}

/* 按上层 RPM 口径收紧速度值，避免传入超范围。 */
static uint16_t emm42_clamp_speed_rpm(uint16_t speed)
{
    if (speed > EMM42_SPEED_MAX_RPM) {
        return EMM42_SPEED_MAX_RPM;
    }

    return speed;
}

/* 将上层 RPM 口径换算为协议层 0.1RPM 字段。 */
static uint16_t emm42_speed_rpm_to_proto(uint16_t speed_rpm)
{
    uint32_t proto_speed = (uint32_t)emm42_clamp_speed_rpm(speed_rpm) *
                           (uint32_t)EMM42_SPEED_SCALE_X10;

    if (proto_speed > (uint32_t)EMM42_SPEED_MAX_PROTO) {
        proto_speed = (uint32_t)EMM42_SPEED_MAX_PROTO;
    }

    return (uint16_t)proto_speed;
}

/* 按 Emm 固件口径收紧加速度档位。 */
static uint8_t emm42_clamp_accel_grade(uint8_t acceleration)
{
    if (acceleration > EMM42_ACCEL_MAX_GRADE) {
        return EMM42_ACCEL_MAX_GRADE;
    }

    return acceleration;
}

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 双轴步进电机位置更新任务
 * @note  保留旧任务入口；当前二维平台主控制路径已切换到速度模式
 */
void Emm42_RunCommandTask(void)
{
    /* 旧的位置模式路径已废弃，避免误用保留为空函数。 */
}

/**
 * @brief 组包并发送电机使能或失能命令
 * @param id 电机轴 ID
 * @param enable_status 使能状态
 */
void Emm42_SendEnableCommand(uint8_t axis_id, uint8_t enable_status)
{
    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_ENABLE,
        0xABu,
        enable_status,
        EMM42_SYNC_FLAG,
        EMM42_CHECK_BYTE
    };

    emm42_send_mgmt_frame(cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 组包并发送“读取电机实时速度”查询命令（0x35）
 * @param axis_id 目标电机轴地址
 * @note  帧格式：[Addr][0x35][0x6B]；返回帧由总线解析并缓存
 */
void Emm42_SendReadSpeedCommand(uint8_t axis_id)
{
    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_READ_SPEED,
        EMM42_CHECK_BYTE
    };

    emm42_send_mgmt_frame(cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 组包并发送速度模式命令
 * @param id 电机轴 ID
 * @param direction 转动方向
 * @param speed 速度字段
 * @param acceleration 加速度字段
 */
void Emm42_SendSpeedCommand(uint8_t axis_id,
                            uint8_t direction,
                            uint16_t speed,
                            uint8_t acceleration)
{
    speed = emm42_speed_rpm_to_proto(speed);
    acceleration = emm42_clamp_accel_grade(acceleration);

    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_SPEED,
        direction,
        (uint8_t)(speed >> 8),
        (uint8_t)(speed),
        acceleration,
        EMM42_SYNC_FLAG,
        EMM42_CHECK_BYTE
    };

    emm42_submit_control_frame(axis_id, cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 组包并发送位置模式命令
 * @param id 电机轴 ID
 * @param direction 转动方向
 * @param speed 速度字段
 * @param acceleration 加速度字段
 * @param pulses 脉冲数
 * @param mode 位置模式：0=相对，1=绝对
 * @note  位置模式链路固定按 Acc=0 发送，避免加减速缓冲影响定位一致性
 */
void Emm42_SendPositionCommand(uint8_t axis_id,
                               uint8_t direction,
                               uint16_t speed,
                               uint8_t acceleration,
                               uint32_t pulses,
                               uint8_t mode)
{
    speed = emm42_speed_rpm_to_proto(speed);
    (void)acceleration;
    acceleration = EMM42_POSITION_ACCEL_FIXED;

    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_POSITION,
        direction,
        (uint8_t)(speed >> 8),
        (uint8_t)(speed),
        acceleration,
        (uint8_t)(pulses >> 24),
        (uint8_t)(pulses >> 16),
        (uint8_t)(pulses >> 8),
        (uint8_t)(pulses),
        mode,
        EMM42_SYNC_FLAG,
        EMM42_CHECK_BYTE
    };

    emm42_submit_control_frame(axis_id, cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 使能两个配置好的 EMM42 电机轴
 */
void Emm42_EnableAll(void)
{
    Emm42_SendEnableCommand(EMM42_AXIS_ID_Y, EMM42_ENABLE_ON);
    Emm42_SendEnableCommand(EMM42_AXIS_ID_X, EMM42_ENABLE_ON);
}

/**
 * @brief 关闭两个配置好的 EMM42 电机轴
 */
void Emm42_DisableAll(void)
{
    Emm42_SendEnableCommand(EMM42_AXIS_ID_Y, EMM42_ENABLE_OFF);
    Emm42_SendEnableCommand(EMM42_AXIS_ID_X, EMM42_ENABLE_OFF);
}

/**
 * @brief 将全部电机轴当前位置设为零点
 */
void Emm42_SetAllAxesZero(void)
{
    Emm42_SetZeroPosition(EMM42_AXIS_ID_Y);
    Emm42_SetZeroPosition(EMM42_AXIS_ID_X);
}

/**
 * @brief 发送单轴相对位移命令
 * @param axis 目标轴
 * @param pulses 相对脉冲数，正负号决定方向
 * @param speed 速度字段
 * @param acceleration 加速度字段
 */
void Emm42_MoveRelative(Emm42_Axis_e axis,
                        int32_t pulses,
                        uint16_t speed,
                        uint8_t acceleration)
{
    uint32_t pulse_count = 0u;
    uint8_t direction = EMM42_DIR_CW;

    if ((axis != EMM42_AXIS_Y) && (axis != EMM42_AXIS_X)) {
        return;
    }

    if (pulses == 0) {
        return;
    }

    if (pulses < 0) {
        direction = EMM42_DIR_CCW;
        pulse_count = (uint32_t)(-pulses);
    } else {
        pulse_count = (uint32_t)pulses;
    }

    Emm42_SendPositionCommand((uint8_t)axis,
                              direction,
                              speed,
                              acceleration,
                              pulse_count,
                              EMM42_POSITION_MODE_RELATIVE);
}

/**
 * @brief 发送单轴绝对位置命令
 * @param axis 目标轴
 * @param position_pulses 绝对位置脉冲数，按 256 细分口径（1 rev = 51200 pulse）
 * @param speed 上层 RPM
 * @note  题目里做绝对坐标定位时，优先走这个接口；推荐参数 V=30 RPM，A=0。
 * @note  固定使用 DIR=00、ACC=00、MODE=01、SYNC=00，对齐当前 EMM42 绝对位置协议口径。
 */
void Emm42_MoveAbsolute(Emm42_Axis_e axis,
                        uint32_t position_pulses,
                        uint16_t speed)
{
    if ((axis != EMM42_AXIS_Y) && (axis != EMM42_AXIS_X)) {
        return;
    }

    Emm42_SendPositionCommand((uint8_t)axis,
                              EMM42_POSITION_DIR_ABSOLUTE,
                              speed,
                              EMM42_POSITION_ACCEL_FIXED,
                              position_pulses,
                              EMM42_POSITION_MODE_ABSOLUTE);
}

/**
 * @brief 发送“当前位置设为原点”命令
 * @param id 电机轴 ID
 */
void Emm42_SetZeroPosition(uint8_t axis_id)
{
    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_ORIGIN_SET,
        EMM42_ORIGIN_SET_FIXED_0,
        EMM42_ORIGIN_SET_FIXED_1,
        EMM42_CHECK_BYTE
    };

    emm42_send_mgmt_frame(cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 发送“执行回原点”命令
 * @param id 电机轴 ID
 */
void Emm42_StartHoming(uint8_t axis_id)
{
    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_ORIGIN_RUN,
        EMM42_ORIGIN_RUN_FIXED_0,
        EMM42_ORIGIN_RUN_FIXED_1,
        EMM42_CHECK_BYTE
    };

    emm42_send_mgmt_frame(cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 发送“退出回零模式”命令
 * @param id 电机轴 ID
 */
void Emm42_ExitHoming(uint8_t axis_id)
{
    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_ORIGIN_QUIT,
        EMM42_ORIGIN_QUIT_FIXED_0,
        EMM42_CHECK_BYTE
    };

    emm42_send_mgmt_frame(cmd, (uint8_t)sizeof(cmd));
}

/**
 * @brief 发送 Emm 固件 PID 参数修改命令
 * @param axis_id 电机轴 ID
 * @param save_to_flash 0=不保存，1=写入 Flash
 * @param kp 比例参数，按 32 位整数口径编码
 * @param ki 积分参数，按 32 位整数口径编码
 * @param kd 微分参数，按 32 位整数口径编码
 */
void Emm42_SendPidConfigCommand(uint8_t axis_id,
                                uint8_t save_to_flash,
                                uint32_t kp,
                                uint32_t ki,
                                uint32_t kd)
{
    uint8_t cmd[] = {
        axis_id,
        EMM42_CMD_PID_CFG,
        EMM42_CMD_PID_CFG_AUX,
        (uint8_t)((save_to_flash != 0u) ? 1u : 0u),
        (uint8_t)(kp >> 24),
        (uint8_t)(kp >> 16),
        (uint8_t)(kp >> 8),
        (uint8_t)(kp),
        (uint8_t)(ki >> 24),
        (uint8_t)(ki >> 16),
        (uint8_t)(ki >> 8),
        (uint8_t)(ki),
        (uint8_t)(kd >> 24),
        (uint8_t)(kd >> 16),
        (uint8_t)(kd >> 8),
        (uint8_t)(kd),
        EMM42_CHECK_BYTE
    };

    emm42_send_mgmt_frame(cmd, (uint8_t)sizeof(cmd));
}
