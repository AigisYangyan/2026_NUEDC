/**
 * @file    gimbal_stepbus.c
 * @brief   云台服务内私有：最小步进 TX 派发实现（契约 §21.3）
 */
#include "app/service/gimbal/gimbal_stepbus.h"

#include "driver/board_uart/stepmotor_uart.h"
#include "driver/step_motor/emm42.h"

#include <stddef.h>
#include <stdint.h>

#define GIMBAL_STEPBUS_FRAME_MAX 32u   /* ≥ 最长 emm42 帧（PID cfg 17B），对齐 stepmotor_uart TX buf */
#define GIMBAL_STEPBUS_RX_CHUNK  32u

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

bool GimbalStepbus_TrySendRelative(GimbalStepbus_Axis axis, int32_t pulses, uint16_t speed_rpm)
{
    uint8_t frame[GIMBAL_STEPBUS_FRAME_MAX];
    uint8_t frame_len = 0u;
    uint8_t direction = EMM42_DIR_CW;
    uint32_t magnitude = 0u;

    if (pulses == 0) {
        return false;
    }
    if (StepmotorUart_IsTxIdle() == false) {
        return false;
    }

    if (pulses < 0) {
        direction = EMM42_DIR_CCW;
        magnitude = (uint32_t)(-(int64_t)pulses);
    } else {
        direction = EMM42_DIR_CW;
        magnitude = (uint32_t)pulses;
    }

    if (Emm42_BuildPositionFrame(gimbal_stepbus_axis_id(axis),
                                 direction,
                                 speed_rpm,
                                 EMM42_POSITION_ACCEL_FIXED,
                                 magnitude,
                                 EMM42_POSITION_MODE_RELATIVE,
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

bool GimbalStepbus_TrySendSetZero(GimbalStepbus_Axis axis)
{
    uint8_t frame[GIMBAL_STEPBUS_FRAME_MAX];
    uint8_t frame_len = 0u;

    if (StepmotorUart_IsTxIdle() == false) {
        return false;
    }

    if (Emm42_BuildSetZeroFrame(gimbal_stepbus_axis_id(axis), frame, &frame_len) == false) {
        return false;
    }

    return StepmotorUart_TryWrite(frame, (uint32_t)frame_len);
}
