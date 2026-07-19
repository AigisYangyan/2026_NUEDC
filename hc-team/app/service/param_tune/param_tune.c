/**
 * @file    param_tune.c
 * @brief   按钮动态调参持久化编排（Model A：无增益副本，全委派 line_follow + param_store）。
 *
 * blob payload（13B，小端）：[0] schema_ver=1、[1..4] kp_milli、[5..8] ki_milli、[9..12] kd_milli。
 * schema_ver 是 payload 语义版本（param_store 对其不可知）；不符→用默认。
 *
 * 换算唯一所有者：milli↔float ×1000（四舍五入）。已应用增益唯一属 line_follow，不在此存副本。
 */
#include "app/service/param_tune/param_tune.h"

#include "app/service/line_follow/line_follow.h"
#include "driver/param_store/param_store.h"

#define TUNE_SCHEMA_VER   1u
#define TUNE_PAYLOAD_LEN  13u

/* 默认增益（milli，占位=0：未标定，上车 K1 上调）。 */
#define TUNE_DEFAULT_KP_MILLI 0
#define TUNE_DEFAULT_KI_MILLI 0
#define TUNE_DEFAULT_KD_MILLI 0

static int32_t float_to_milli(float v)
{
    float scaled = v * 1000.0f;
    return (int32_t)(scaled + ((v >= 0.0f) ? 0.5f : -0.5f));   /* 四舍五入 */
}

static float milli_to_float(int32_t m)
{
    return (float)m / 1000.0f;
}

static void put_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xFFu);
    p[1] = (uint8_t)((u >> 8) & 0xFFu);
    p[2] = (uint8_t)((u >> 16) & 0xFFu);
    p[3] = (uint8_t)((u >> 24) & 0xFFu);
}

static int32_t get_i32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (int32_t)u;
}

/* 以三个 milli 增益应用到 line_follow（唯一应用出口）。 */
static void apply_milli(int32_t kp_m, int32_t ki_m, int32_t kd_m)
{
    LineFollow_SetGains(milli_to_float(kp_m), milli_to_float(ki_m), milli_to_float(kd_m));
}

void ParamTune_Init(void)
{
    uint8_t payload[TUNE_PAYLOAD_LEN];

    if (ParamStore_Read(payload, TUNE_PAYLOAD_LEN) && (payload[0] == TUNE_SCHEMA_VER)) {
        apply_milli(get_i32(&payload[1]), get_i32(&payload[5]), get_i32(&payload[9]));
    } else {
        apply_milli(TUNE_DEFAULT_KP_MILLI, TUNE_DEFAULT_KI_MILLI, TUNE_DEFAULT_KD_MILLI);
    }
}

int32_t ParamTune_GetKp_milli(void)
{
    float kp, ki, kd;
    LineFollow_GetGains(&kp, &ki, &kd);
    return float_to_milli(kp);
}

int32_t ParamTune_GetKi_milli(void)
{
    float kp, ki, kd;
    LineFollow_GetGains(&kp, &ki, &kd);
    return float_to_milli(ki);
}

int32_t ParamTune_GetKd_milli(void)
{
    float kp, ki, kd;
    LineFollow_GetGains(&kp, &ki, &kd);
    return float_to_milli(kd);
}

void ParamTune_SetKp_milli(int32_t v)
{
    float kp, ki, kd;
    LineFollow_GetGains(&kp, &ki, &kd);
    LineFollow_SetGains(milli_to_float(v), ki, kd);
}

void ParamTune_SetKi_milli(int32_t v)
{
    float kp, ki, kd;
    LineFollow_GetGains(&kp, &ki, &kd);
    LineFollow_SetGains(kp, milli_to_float(v), kd);
}

void ParamTune_SetKd_milli(int32_t v)
{
    float kp, ki, kd;
    LineFollow_GetGains(&kp, &ki, &kd);
    LineFollow_SetGains(kp, ki, milli_to_float(v));
}

void ParamTune_Save(void)
{
    uint8_t payload[TUNE_PAYLOAD_LEN];
    float kp, ki, kd;

    LineFollow_GetGains(&kp, &ki, &kd);
    payload[0] = (uint8_t)TUNE_SCHEMA_VER;
    put_i32(&payload[1], float_to_milli(kp));
    put_i32(&payload[5], float_to_milli(ki));
    put_i32(&payload[9], float_to_milli(kd));
    (void)ParamStore_Save(payload, TUNE_PAYLOAD_LEN);
}
