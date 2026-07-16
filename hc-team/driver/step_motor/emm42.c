/**
 * @file    emm42.c
 * @brief   EMM42 步进驱动协议纯组包实现
 *
 * 本文件只负责把调用参数编码为协议帧，不再反向依赖 App 运输层。
 * 旧的 Emm42_Send* 兼容包装由 stepmotor_bus.c 提供并负责入队。
 */

#include "emm42.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMM42_CMD_ORIGIN_SET      0x93u
#define EMM42_CMD_ORIGIN_RUN      0x9Au
#define EMM42_CMD_ORIGIN_QUIT     0x9Cu
#define EMM42_CMD_PID_CFG_AUX     0xC3u
#define EMM42_ORIGIN_SET_FIXED_0  0x88u
#define EMM42_ORIGIN_SET_FIXED_1  0x01u
#define EMM42_ORIGIN_RUN_FIXED_0  0x00u
#define EMM42_ORIGIN_RUN_FIXED_1  0x00u
#define EMM42_ORIGIN_QUIT_FIXED_0 0x48u

int g_emm42_default_acceleration = 0;
int g_emm42_default_speed = 30;

static uint16_t emm42_clamp_speed_rpm(uint16_t speed)
{
    if (speed > EMM42_SPEED_MAX_RPM) {
        return EMM42_SPEED_MAX_RPM;
    }

    return speed;
}

static uint16_t emm42_speed_rpm_to_proto(uint16_t speed_rpm)
{
    uint32_t proto_speed =
        (uint32_t)emm42_clamp_speed_rpm(speed_rpm) * (uint32_t)EMM42_SPEED_SCALE_X10;

    if (proto_speed > (uint32_t)EMM42_SPEED_MAX_PROTO) {
        proto_speed = (uint32_t)EMM42_SPEED_MAX_PROTO;
    }

    return (uint16_t)proto_speed;
}

static uint8_t emm42_clamp_accel_grade(uint8_t acceleration)
{
    if (acceleration > EMM42_ACCEL_MAX_GRADE) {
        return EMM42_ACCEL_MAX_GRADE;
    }

    return acceleration;
}

static bool emm42_prepare_out(uint8_t *out, uint8_t *out_len, uint8_t frame_len)
{
    if ((out == NULL) || (out_len == NULL)) {
        return false;
    }

    *out_len = frame_len;
    return true;
}

bool Emm42_BuildEnableFrame(uint8_t axis_id,
                            uint8_t enable_status,
                            uint8_t *out,
                            uint8_t *out_len)
{
    if (emm42_prepare_out(out, out_len, 6u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_ENABLE;
    out[2] = 0xABu;
    out[3] = enable_status;
    out[4] = EMM42_SYNC_FLAG;
    out[5] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildReadSpeedFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len)
{
    if (emm42_prepare_out(out, out_len, 3u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_READ_SPEED;
    out[2] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildSpeedFrame(uint8_t axis_id,
                           uint8_t direction,
                           uint16_t speed_rpm,
                           uint8_t acceleration,
                           uint8_t *out,
                           uint8_t *out_len)
{
    uint16_t speed_proto = emm42_speed_rpm_to_proto(speed_rpm);

    if (emm42_prepare_out(out, out_len, 8u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_SPEED;
    out[2] = direction;
    out[3] = (uint8_t)(speed_proto >> 8);
    out[4] = (uint8_t)(speed_proto);
    out[5] = emm42_clamp_accel_grade(acceleration);
    out[6] = EMM42_SYNC_FLAG;
    out[7] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildPositionFrame(uint8_t axis_id,
                              uint8_t direction,
                              uint16_t speed_rpm,
                              uint8_t acceleration,
                              uint32_t pulses,
                              uint8_t mode,
                              uint8_t *out,
                              uint8_t *out_len)
{
    uint16_t speed_proto = emm42_speed_rpm_to_proto(speed_rpm);

    (void)acceleration;
    if (emm42_prepare_out(out, out_len, 13u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_POSITION;
    out[2] = direction;
    out[3] = (uint8_t)(speed_proto >> 8);
    out[4] = (uint8_t)(speed_proto);
    out[5] = EMM42_POSITION_ACCEL_FIXED;
    out[6] = (uint8_t)(pulses >> 24);
    out[7] = (uint8_t)(pulses >> 16);
    out[8] = (uint8_t)(pulses >> 8);
    out[9] = (uint8_t)(pulses);
    out[10] = mode;
    out[11] = EMM42_SYNC_FLAG;
    out[12] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildSetZeroFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len)
{
    if (emm42_prepare_out(out, out_len, 5u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_ORIGIN_SET;
    out[2] = EMM42_ORIGIN_SET_FIXED_0;
    out[3] = EMM42_ORIGIN_SET_FIXED_1;
    out[4] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildStartHomingFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len)
{
    if (emm42_prepare_out(out, out_len, 5u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_ORIGIN_RUN;
    out[2] = EMM42_ORIGIN_RUN_FIXED_0;
    out[3] = EMM42_ORIGIN_RUN_FIXED_1;
    out[4] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildExitHomingFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len)
{
    if (emm42_prepare_out(out, out_len, 4u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_ORIGIN_QUIT;
    out[2] = EMM42_ORIGIN_QUIT_FIXED_0;
    out[3] = EMM42_CHECK_BYTE;
    return true;
}

bool Emm42_BuildPidConfigFrame(uint8_t axis_id,
                               uint8_t save_to_flash,
                               uint32_t kp,
                               uint32_t ki,
                               uint32_t kd,
                               uint8_t *out,
                               uint8_t *out_len)
{
    if (emm42_prepare_out(out, out_len, 17u) == false) {
        return false;
    }

    out[0] = axis_id;
    out[1] = EMM42_CMD_PID_CFG;
    out[2] = EMM42_CMD_PID_CFG_AUX;
    out[3] = (uint8_t)((save_to_flash != 0u) ? 1u : 0u);
    out[4] = (uint8_t)(kp >> 24);
    out[5] = (uint8_t)(kp >> 16);
    out[6] = (uint8_t)(kp >> 8);
    out[7] = (uint8_t)(kp);
    out[8] = (uint8_t)(ki >> 24);
    out[9] = (uint8_t)(ki >> 16);
    out[10] = (uint8_t)(ki >> 8);
    out[11] = (uint8_t)(ki);
    out[12] = (uint8_t)(kd >> 24);
    out[13] = (uint8_t)(kd >> 16);
    out[14] = (uint8_t)(kd >> 8);
    out[15] = (uint8_t)(kd);
    out[16] = EMM42_CHECK_BYTE;
    return true;
}

void Emm42_RunCommandTask(void)
{
}
