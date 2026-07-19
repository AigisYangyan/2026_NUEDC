/**
 * @file    param_tune.c
 * @brief   按钮动态调参持久化编排（Model A：无值副本，委派 line_follow + motion + param_store）。
 *
 * blob payload（schema_ver 2，33B，小端）：
 *   [0]      schema_ver=2（payload 语义版本，param_store 对其不可知）
 *   [1..4]   kp_milli   [5..8] ki_milli   [9..12] kd_milli        （循迹外环增益，委派 line_follow）
 *   [13..16] cruise_milli [17..20] start_milli
 *   [21..24] accel_milli  [25..28] decel_milli                    （motion 剖面参数，委派 motion）
 *   [29..32] dist_mm                                              （测试距离，本模块自持）
 * 旧 v1(13B) 记录经 ParamStore_Read(len=33) 长度不符→false→全默认（一次性丢旧 LF 增益）。
 *
 * 换算唯一所有者：milli↔float ×1000（四舍五入）。已应用增益唯一属 line_follow、剖面参数唯一属
 * motion，均不在此存副本；仅测试距离 s_dist_mm 本模块自持（无 Service 家）。
 */
#include "app/service/param_tune/param_tune.h"

#include "app/service/line_follow/line_follow.h"
#include "app/service/motion/motion.h"
#include "driver/param_store/param_store.h"

#define TUNE_SCHEMA_VER   2u
#define TUNE_PAYLOAD_LEN  33u

/* 默认值（占位，UNCALIBRATED）：LF 增益 0（安全，不纠偏）；剖面保守低速可跑；距离 1000mm。 */
#define TUNE_DEFAULT_KP_MILLI     0
#define TUNE_DEFAULT_KI_MILLI     0
#define TUNE_DEFAULT_KD_MILLI     0
#define TUNE_DEFAULT_CRUISE_MILLI 200   /* 0.20 m/s 保守巡航 */
#define TUNE_DEFAULT_START_MILLI  80    /* 0.08 m/s 起步 */
#define TUNE_DEFAULT_ACCEL_MILLI  300   /* 0.30 m/s^2 */
#define TUNE_DEFAULT_DECEL_MILLI  300   /* 0.30 m/s^2 */
#define TUNE_DEFAULT_DIST_MM      1000  /* 100 cm */

/* 测试距离：本模块唯一自持值（测试设定量无 Service 所有者）。 */
static int32_t s_dist_mm = TUNE_DEFAULT_DIST_MM;

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
static void apply_gains_milli(int32_t kp_m, int32_t ki_m, int32_t kd_m)
{
    LineFollow_SetGains(milli_to_float(kp_m), milli_to_float(ki_m), milli_to_float(kd_m));
}

/* 以四个 milli 剖面参数应用到 motion（唯一应用出口）。 */
static void apply_profile_milli(int32_t cruise_m, int32_t start_m, int32_t accel_m, int32_t decel_m)
{
    Motion_SetProfileParams(milli_to_float(cruise_m), milli_to_float(start_m),
                            milli_to_float(accel_m), milli_to_float(decel_m));
}

void ParamTune_Init(void)
{
    uint8_t payload[TUNE_PAYLOAD_LEN];

    if (ParamStore_Read(payload, TUNE_PAYLOAD_LEN) && (payload[0] == TUNE_SCHEMA_VER)) {
        apply_gains_milli(get_i32(&payload[1]), get_i32(&payload[5]), get_i32(&payload[9]));
        apply_profile_milli(get_i32(&payload[13]), get_i32(&payload[17]),
                            get_i32(&payload[21]), get_i32(&payload[25]));
        s_dist_mm = get_i32(&payload[29]);
    } else {
        apply_gains_milli(TUNE_DEFAULT_KP_MILLI, TUNE_DEFAULT_KI_MILLI, TUNE_DEFAULT_KD_MILLI);
        apply_profile_milli(TUNE_DEFAULT_CRUISE_MILLI, TUNE_DEFAULT_START_MILLI,
                            TUNE_DEFAULT_ACCEL_MILLI, TUNE_DEFAULT_DECEL_MILLI);
        s_dist_mm = TUNE_DEFAULT_DIST_MM;
    }
}

/* ---- 循迹外环增益（委派 line_follow）------------------------------------- */

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

/* ---- motion 剖面参数（委派 motion）--------------------------------------- */

int32_t ParamTune_GetCruise_milli(void)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    return float_to_milli(cruise);
}

int32_t ParamTune_GetStart_milli(void)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    return float_to_milli(start);
}

int32_t ParamTune_GetAccel_milli(void)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    return float_to_milli(accel);
}

int32_t ParamTune_GetDecel_milli(void)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    return float_to_milli(decel);
}

void ParamTune_SetCruise_milli(int32_t v)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    Motion_SetProfileParams(milli_to_float(v), start, accel, decel);
}

void ParamTune_SetStart_milli(int32_t v)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    Motion_SetProfileParams(cruise, milli_to_float(v), accel, decel);
}

void ParamTune_SetAccel_milli(int32_t v)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    Motion_SetProfileParams(cruise, start, milli_to_float(v), decel);
}

void ParamTune_SetDecel_milli(int32_t v)
{
    float cruise, start, accel, decel;
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    Motion_SetProfileParams(cruise, start, accel, milli_to_float(v));
}

/* ---- 测试距离（本模块自持）----------------------------------------------- */

int32_t ParamTune_GetDist_mm(void)
{
    return s_dist_mm;
}

void ParamTune_SetDist_mm(int32_t v)
{
    s_dist_mm = v;
}

/* ---- 存盘（序列化 schema_ver 2 全 payload）-------------------------------- */

void ParamTune_Save(void)
{
    uint8_t payload[TUNE_PAYLOAD_LEN];
    float kp, ki, kd;
    float cruise, start, accel, decel;

    LineFollow_GetGains(&kp, &ki, &kd);
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);

    payload[0] = (uint8_t)TUNE_SCHEMA_VER;
    put_i32(&payload[1], float_to_milli(kp));
    put_i32(&payload[5], float_to_milli(ki));
    put_i32(&payload[9], float_to_milli(kd));
    put_i32(&payload[13], float_to_milli(cruise));
    put_i32(&payload[17], float_to_milli(start));
    put_i32(&payload[21], float_to_milli(accel));
    put_i32(&payload[25], float_to_milli(decel));
    put_i32(&payload[29], s_dist_mm);
    (void)ParamStore_Save(payload, TUNE_PAYLOAD_LEN);
}
