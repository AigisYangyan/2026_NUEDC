/**
 * @file    param_tune.c
 * @brief   按钮动态调参持久化编排（Model A：无值副本，委派 line_follow/chassis/motion + param_store）。
 *
 * blob payload（schema_ver 3，65B，小端）：
 *   [0]      schema_ver=3（payload 语义版本，param_store 对其不可知）
 *   [1..4]   kp_milli   [5..8] ki_milli   [9..12] kd_milli        （循迹外环增益，委派 line_follow）
 *   [13..16] cruise_milli [17..20] start_milli
 *   [21..24] accel_milli  [25..28] decel_milli                    （motion 剖面参数，委派 motion）
 *   [29..32] dist_mm                                              （测试距离，本模块自持）
 *   [33..44] ckp/cki/ckd_milli                                    （底盘速度环增益，委派 chassis，双轮同值）
 *   [45..60] hkp/hki/hkd/htkp_milli                               （航向保持三增益+定角转 kp，委派 motion）
 *   [61..64] turn_deg                                             （转弯测试角，本模块自持）
 * 旧 v1(13B)/v2(33B) 记录经 ParamStore_Read(len=65) 长度不符→false→全默认（一次性失效，先例口径）。
 *
 * 换算唯一所有者：milli↔float ×1000（四舍五入）。已应用增益唯一属 line_follow/chassis/motion，
 * 均不在此存副本；仅测试距离 s_dist_mm 与转弯测试角 s_turn_deg 本模块自持（无 Service 家）。
 */
#include "app/service/param_tune/param_tune.h"

#include "app/service/chassis/chassis.h"
#include "app/service/line_follow/line_follow.h"
#include "app/service/motion/motion.h"
#include "driver/param_store/param_store.h"

#define TUNE_SCHEMA_VER   3u
#define TUNE_PAYLOAD_LEN  65u

/* 默认值（占位，UNCALIBRATED）：LF 增益 0（安全，不纠偏）；剖面保守低速可跑；距离 1000mm。 */
#define TUNE_DEFAULT_KP_MILLI     0
#define TUNE_DEFAULT_KI_MILLI     0
#define TUNE_DEFAULT_KD_MILLI     0
#define TUNE_DEFAULT_CRUISE_MILLI 200   /* 0.20 m/s 保守巡航 */
#define TUNE_DEFAULT_START_MILLI  80    /* 0.08 m/s 起步 */
#define TUNE_DEFAULT_ACCEL_MILLI  300   /* 0.30 m/s^2 */
#define TUNE_DEFAULT_DECEL_MILLI  300   /* 0.30 m/s^2 */
#define TUNE_DEFAULT_DIST_MM      1000  /* 100 cm */
/* 底盘/航向增益默认 0：无实测依据不拍系数（上车 SpeedTune/TurnTest 调出后 SAVE 录入）。 */
#define TUNE_DEFAULT_CKP_MILLI    0
#define TUNE_DEFAULT_CKI_MILLI    0
#define TUNE_DEFAULT_CKD_MILLI    0
#define TUNE_DEFAULT_HKP_MILLI    0
#define TUNE_DEFAULT_HKI_MILLI    0
#define TUNE_DEFAULT_HKD_MILLI    0
#define TUNE_DEFAULT_HTKP_MILLI   0
#define TUNE_DEFAULT_TURN_DEG     90

/* 测试距离/转弯测试角：本模块自持值（测试设定量无 Service 所有者）。 */
static int32_t s_dist_mm = TUNE_DEFAULT_DIST_MM;
static int32_t s_turn_deg = TUNE_DEFAULT_TURN_DEG;

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

/* 底盘速度环增益：双轮同值应用（唯一应用出口）。 */
static void apply_chassis_milli(int32_t kp_m, int32_t ki_m, int32_t kd_m)
{
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, milli_to_float(kp_m),
                          milli_to_float(ki_m), milli_to_float(kd_m));
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, milli_to_float(kp_m),
                          milli_to_float(ki_m), milli_to_float(kd_m));
}

/* 航向调参应用到 motion（唯一应用出口）。 */
static void apply_heading_milli(int32_t hkp_m, int32_t hki_m, int32_t hkd_m, int32_t htkp_m)
{
    Motion_SetHeadingTuning(milli_to_float(hkp_m), milli_to_float(hki_m),
                            milli_to_float(hkd_m), milli_to_float(htkp_m));
}

void ParamTune_Init(void)
{
    uint8_t payload[TUNE_PAYLOAD_LEN];

    if (ParamStore_Read(payload, TUNE_PAYLOAD_LEN) && (payload[0] == TUNE_SCHEMA_VER)) {
        apply_gains_milli(get_i32(&payload[1]), get_i32(&payload[5]), get_i32(&payload[9]));
        apply_profile_milli(get_i32(&payload[13]), get_i32(&payload[17]),
                            get_i32(&payload[21]), get_i32(&payload[25]));
        s_dist_mm = get_i32(&payload[29]);
        apply_chassis_milli(get_i32(&payload[33]), get_i32(&payload[37]), get_i32(&payload[41]));
        apply_heading_milli(get_i32(&payload[45]), get_i32(&payload[49]),
                            get_i32(&payload[53]), get_i32(&payload[57]));
        s_turn_deg = get_i32(&payload[61]);
    } else {
        apply_gains_milli(TUNE_DEFAULT_KP_MILLI, TUNE_DEFAULT_KI_MILLI, TUNE_DEFAULT_KD_MILLI);
        apply_profile_milli(TUNE_DEFAULT_CRUISE_MILLI, TUNE_DEFAULT_START_MILLI,
                            TUNE_DEFAULT_ACCEL_MILLI, TUNE_DEFAULT_DECEL_MILLI);
        s_dist_mm = TUNE_DEFAULT_DIST_MM;
        apply_chassis_milli(TUNE_DEFAULT_CKP_MILLI, TUNE_DEFAULT_CKI_MILLI, TUNE_DEFAULT_CKD_MILLI);
        apply_heading_milli(TUNE_DEFAULT_HKP_MILLI, TUNE_DEFAULT_HKI_MILLI,
                            TUNE_DEFAULT_HKD_MILLI, TUNE_DEFAULT_HTKP_MILLI);
        s_turn_deg = TUNE_DEFAULT_TURN_DEG;
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

/* ---- 底盘速度环增益（委派 chassis，双轮同值；读回取左轮）------------------- */

int32_t ParamTune_GetCKp_milli(void)
{
    float kp = 0.0f, ki = 0.0f, kd = 0.0f;
    Chassis_GetSpeedGains(CHASSIS_SIDE_LEFT, &kp, &ki, &kd);
    return float_to_milli(kp);
}

int32_t ParamTune_GetCKi_milli(void)
{
    float kp = 0.0f, ki = 0.0f, kd = 0.0f;
    Chassis_GetSpeedGains(CHASSIS_SIDE_LEFT, &kp, &ki, &kd);
    return float_to_milli(ki);
}

int32_t ParamTune_GetCKd_milli(void)
{
    float kp = 0.0f, ki = 0.0f, kd = 0.0f;
    Chassis_GetSpeedGains(CHASSIS_SIDE_LEFT, &kp, &ki, &kd);
    return float_to_milli(kd);
}

void ParamTune_SetCKp_milli(int32_t v)
{
    apply_chassis_milli(v, ParamTune_GetCKi_milli(), ParamTune_GetCKd_milli());
}

void ParamTune_SetCKi_milli(int32_t v)
{
    apply_chassis_milli(ParamTune_GetCKp_milli(), v, ParamTune_GetCKd_milli());
}

void ParamTune_SetCKd_milli(int32_t v)
{
    apply_chassis_milli(ParamTune_GetCKp_milli(), ParamTune_GetCKi_milli(), v);
}

/* ---- motion 航向调参（委派 motion）--------------------------------------- */

int32_t ParamTune_GetHKp_milli(void)
{
    float hkp = 0.0f, hki = 0.0f, hkd = 0.0f, htkp = 0.0f;
    Motion_GetHeadingTuning(&hkp, &hki, &hkd, &htkp);
    return float_to_milli(hkp);
}

int32_t ParamTune_GetHKi_milli(void)
{
    float hkp = 0.0f, hki = 0.0f, hkd = 0.0f, htkp = 0.0f;
    Motion_GetHeadingTuning(&hkp, &hki, &hkd, &htkp);
    return float_to_milli(hki);
}

int32_t ParamTune_GetHKd_milli(void)
{
    float hkp = 0.0f, hki = 0.0f, hkd = 0.0f, htkp = 0.0f;
    Motion_GetHeadingTuning(&hkp, &hki, &hkd, &htkp);
    return float_to_milli(hkd);
}

int32_t ParamTune_GetHTKp_milli(void)
{
    float hkp = 0.0f, hki = 0.0f, hkd = 0.0f, htkp = 0.0f;
    Motion_GetHeadingTuning(&hkp, &hki, &hkd, &htkp);
    return float_to_milli(htkp);
}

void ParamTune_SetHKp_milli(int32_t v)
{
    apply_heading_milli(v, ParamTune_GetHKi_milli(), ParamTune_GetHKd_milli(),
                        ParamTune_GetHTKp_milli());
}

void ParamTune_SetHKi_milli(int32_t v)
{
    apply_heading_milli(ParamTune_GetHKp_milli(), v, ParamTune_GetHKd_milli(),
                        ParamTune_GetHTKp_milli());
}

void ParamTune_SetHKd_milli(int32_t v)
{
    apply_heading_milli(ParamTune_GetHKp_milli(), ParamTune_GetHKi_milli(), v,
                        ParamTune_GetHTKp_milli());
}

void ParamTune_SetHTKp_milli(int32_t v)
{
    apply_heading_milli(ParamTune_GetHKp_milli(), ParamTune_GetHKi_milli(),
                        ParamTune_GetHKd_milli(), v);
}

/* ---- 转弯测试角（本模块自持）--------------------------------------------- */

int32_t ParamTune_GetTurnDeg(void)
{
    return s_turn_deg;
}

void ParamTune_SetTurnDeg(int32_t v)
{
    s_turn_deg = v;
}

/* ---- 存盘（序列化 schema_ver 3 全 payload）-------------------------------- */

void ParamTune_Save(void)
{
    uint8_t payload[TUNE_PAYLOAD_LEN];
    float kp, ki, kd;
    float cruise, start, accel, decel;
    float ckp = 0.0f, cki = 0.0f, ckd = 0.0f;
    float hkp = 0.0f, hki = 0.0f, hkd = 0.0f, htkp = 0.0f;

    LineFollow_GetGains(&kp, &ki, &kd);
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    Chassis_GetSpeedGains(CHASSIS_SIDE_LEFT, &ckp, &cki, &ckd);
    Motion_GetHeadingTuning(&hkp, &hki, &hkd, &htkp);

    payload[0] = (uint8_t)TUNE_SCHEMA_VER;
    put_i32(&payload[1], float_to_milli(kp));
    put_i32(&payload[5], float_to_milli(ki));
    put_i32(&payload[9], float_to_milli(kd));
    put_i32(&payload[13], float_to_milli(cruise));
    put_i32(&payload[17], float_to_milli(start));
    put_i32(&payload[21], float_to_milli(accel));
    put_i32(&payload[25], float_to_milli(decel));
    put_i32(&payload[29], s_dist_mm);
    put_i32(&payload[33], float_to_milli(ckp));
    put_i32(&payload[37], float_to_milli(cki));
    put_i32(&payload[41], float_to_milli(ckd));
    put_i32(&payload[45], float_to_milli(hkp));
    put_i32(&payload[49], float_to_milli(hki));
    put_i32(&payload[53], float_to_milli(hkd));
    put_i32(&payload[57], float_to_milli(htkp));
    put_i32(&payload[61], s_turn_deg);
    (void)ParamStore_Save(payload, TUNE_PAYLOAD_LEN);
}
