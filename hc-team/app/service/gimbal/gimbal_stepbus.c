/**
 * @file    gimbal_stepbus.c
 * @brief   云台服务内私有：最小步进 TX 派发实现（契约 §21.3）
 */
#include "app/service/gimbal/gimbal_stepbus.h"

#include "driver/board_uart/stepmotor_uart.h"
#include "driver/step_motor/emm42.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GIMBAL_STEPBUS_FRAME_MAX 32u   /* ≥ 最长 emm42 帧（0xAA 双 FC=19B / PID cfg 17B），对齐 TX buf */
#define GIMBAL_STEPBUS_RX_CHUNK  32u
#define GIMBAL_STEPBUS_QPOS_LEN  7u    /* 单条 FC 快速位置帧长 */
#define GIMBAL_STEPBUS_PRESET_ACCEL 0u /* F1 预设加速度档：0=直接启动（小步低速无需斜坡） */

/* 云台轴 → EMM42 器件轴 id（X=2 / Y=1，器件协议事实，emm42.h）。
 * 入参 axis 由同服务 gimbal.c 以字面量 X/Y 或 0/1 循环下标传入，恒有效——不加不可达的 axis 校验。 */
static uint8_t gimbal_stepbus_axis_id(GimbalStepbus_Axis axis)
{
    return (axis == GIMBAL_STEPBUS_AXIS_X) ? (uint8_t)EMM42_AXIS_X : (uint8_t)EMM42_AXIS_Y;
}

void GimbalStepbus_Init(void)
{
    /* 无私有运行时状态：TX 忙/完成事实由 stepmotor_uart 自持（tx_busy/tx_done）。
     * 底层 StepmotorUart_Init 由 system 装配层负责，本模块不重复初始化硬件。 */
}

void GimbalStepbus_Service(void)
{
    uint8_t scratch[GIMBAL_STEPBUS_RX_CHUNK];
    uint32_t read_count = 0u;

    /* 消费 TX 完成事件：真实链路里 DMA 完成 ISR 已清 tx_busy，此处清残留 tx_done 保持无积压。 */
    (void)StepmotorUart_ConsumeTxDone();

    /* drain + discard 步进 RX：界定字节层 FIFO，不解析（步进应答有意不用，视觉是唯一反馈路径）。 */
    do {
        read_count = StepmotorUart_Read(scratch, sizeof(scratch));
    } while (read_count > 0u);
}

bool GimbalStepbus_IsIdle(void)
{
    return StepmotorUart_IsTxIdle();
}

bool GimbalStepbus_TrySendDualAbsolute(int32_t x_pulse, int32_t y_pulse)
{
    uint8_t sub_y[GIMBAL_STEPBUS_QPOS_LEN];
    uint8_t sub_x[GIMBAL_STEPBUS_QPOS_LEN];
    uint8_t sub_y_len = 0u;
    uint8_t sub_x_len = 0u;
    uint8_t subs[2u * GIMBAL_STEPBUS_QPOS_LEN];
    uint8_t frame[GIMBAL_STEPBUS_FRAME_MAX];
    uint8_t frame_len = 0u;

    if (StepmotorUart_IsTxIdle() == false) {
        return false;
    }

    /* 子命令串 = FC_Y ∥ FC_X（Y=器件 addr1 在前，与官方多机示例 addr 升序一致）。 */
    if (Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_Y, y_pulse, sub_y, &sub_y_len) == false) {
        return false;
    }
    if (Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_X, x_pulse, sub_x, &sub_x_len) == false) {
        return false;
    }
    memcpy(subs, sub_y, sub_y_len);
    memcpy(subs + sub_y_len, sub_x, sub_x_len);

    if (Emm42_BuildMultiCmdFrame(subs, (uint8_t)(sub_y_len + sub_x_len),
                                 frame, &frame_len) == false) {
        return false;
    }

    return StepmotorUart_TryWrite(frame, (uint32_t)frame_len);
}

bool GimbalStepbus_TrySendPreset(GimbalStepbus_Axis axis, uint16_t speed_rpm)
{
    uint8_t frame[GIMBAL_STEPBUS_FRAME_MAX];
    uint8_t frame_len = 0u;

    if (StepmotorUart_IsTxIdle() == false) {
        return false;
    }

    /* mode/加速度是云台固定策略（绝对、直启）；速度交 emm42 唯一限幅，本层不夹。 */
    if (Emm42_BuildQPosPresetFrame(gimbal_stepbus_axis_id(axis),
                                   speed_rpm,
                                   GIMBAL_STEPBUS_PRESET_ACCEL,
                                   EMM42_POSITION_MODE_ABSOLUTE,
                                   frame,
                                   &frame_len) == false) {
        return false;
    }

    return StepmotorUart_TryWrite(frame, (uint32_t)frame_len);
}

bool GimbalStepbus_TrySendEnable(GimbalStepbus_Axis axis, bool on)
{
    uint8_t frame[GIMBAL_STEPBUS_FRAME_MAX];
    uint8_t frame_len = 0u;

    if (StepmotorUart_IsTxIdle() == false) {
        return false;
    }

    if (Emm42_BuildEnableFrame(gimbal_stepbus_axis_id(axis),
                               (on == true) ? EMM42_ENABLE_ON : EMM42_ENABLE_OFF,
                               frame,
                               &frame_len) == false) {
        return false;
    }

    return StepmotorUart_TryWrite(frame, (uint32_t)frame_len);
}

bool GimbalStepbus_TrySendClearZero(GimbalStepbus_Axis axis)
{
    uint8_t frame[GIMBAL_STEPBUS_FRAME_MAX];
    uint8_t frame_len = 0u;

    if (StepmotorUart_IsTxIdle() == false) {
        return false;
    }

    if (Emm42_BuildClearPositionFrame(gimbal_stepbus_axis_id(axis), frame, &frame_len) == false) {
        return false;
    }

    return StepmotorUart_TryWrite(frame, (uint32_t)frame_len);
}
