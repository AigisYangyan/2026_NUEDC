/**
 * @file    servo.c
 * @brief   舵机 Driver 实现：软限位夹域 + 斜坡推进 + 自由态播种（唯一所有者）。
 */
#include "driver/servo/servo.h"

#include "driver/servo/servo_hw.h"

#define SERVO_UPDATE_PERIOD_MS   10u
#define SERVO_ANGLE_MIN_DEG      0.0f
#define SERVO_ANGLE_MAX_DEG      180.0f
#define SERVO_DEFAULT_RATE_DPS   300.0f
#define SERVO_PULSE_MIN_US       500.0f
#define SERVO_PULSE_SPAN_US      2000.0f

typedef struct {
    bool  active;        /* true = 在出脉冲保持位置 */
    float current_deg;   /* 斜坡当前角（active 时有效；自由态保留最后值供显示） */
    float target_deg;
    float min_deg;
    float max_deg;
    float rate_dps;
} Servo_State_T;

static Servo_State_T s_servo[SERVO_COUNT];
static bool     s_seeded;
static uint32_t s_period_base_ms;

static float servo_clamp(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void servo_write_pulse(Servo_Id id)
{
    /* 角→脉宽唯一换算点：0~180° 线性映射 500~2500µs，四舍五入到 µs。 */
    float us = SERVO_PULSE_MIN_US
               + (s_servo[id].current_deg * (SERVO_PULSE_SPAN_US / SERVO_ANGLE_MAX_DEG));

    servo_hw_write_pulse_us(id, (uint32_t)(us + 0.5f));
}

void Servo_Init(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)SERVO_COUNT; i++) {
        s_servo[i].active = false;
        s_servo[i].current_deg = 0.0f;
        s_servo[i].target_deg = 0.0f;
        s_servo[i].min_deg = SERVO_ANGLE_MIN_DEG;
        s_servo[i].max_deg = SERVO_ANGLE_MAX_DEG;
        s_servo[i].rate_dps = SERVO_DEFAULT_RATE_DPS;
    }
    s_seeded = false;
    s_period_base_ms = 0u;
    servo_hw_start();   /* 计数器起振，比较 0 = 无脉冲（自由态） */
}

bool Servo_SetLimitsDeg(Servo_Id id, float min_deg, float max_deg)
{
    if ((id >= SERVO_COUNT) || (min_deg < SERVO_ANGLE_MIN_DEG)
        || (max_deg > SERVO_ANGLE_MAX_DEG) || (min_deg >= max_deg)) {
        return false;
    }
    s_servo[id].min_deg = min_deg;
    s_servo[id].max_deg = max_deg;
    /* 收窄时把目标夹回新域（当前角物理上无法跳变，由斜坡自行收敛）。 */
    s_servo[id].target_deg = servo_clamp(s_servo[id].target_deg, min_deg, max_deg);
    return true;
}

bool Servo_SetRateDegPerS(Servo_Id id, float rate)
{
    if ((id >= SERVO_COUNT) || !(rate > 0.0f)) {
        return false;
    }
    s_servo[id].rate_dps = rate;
    return true;
}

bool Servo_SetTargetDeg(Servo_Id id, float deg)
{
    if (id >= SERVO_COUNT) {
        return false;
    }
    deg = servo_clamp(deg, s_servo[id].min_deg, s_servo[id].max_deg);
    s_servo[id].target_deg = deg;
    if (!s_servo[id].active) {
        /* 自由态首命令：当前物理角未知，播种当前=目标（舵机内环全速走位）。 */
        s_servo[id].active = true;
        s_servo[id].current_deg = deg;
    }
    return true;
}

void Servo_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint8_t i;

    if (!s_seeded) {
        s_seeded = true;
        s_period_base_ms = now_ms;
        return;
    }
    elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */
    if (elapsed_ms < SERVO_UPDATE_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    for (i = 0u; i < (uint8_t)SERVO_COUNT; i++) {
        float step;
        float delta;

        if (!s_servo[i].active) {
            continue;
        }
        step = s_servo[i].rate_dps * ((float)elapsed_ms / 1000.0f);
        delta = s_servo[i].target_deg - s_servo[i].current_deg;
        if (delta > step) {
            s_servo[i].current_deg += step;
        } else if (delta < -step) {
            s_servo[i].current_deg -= step;
        } else {
            s_servo[i].current_deg = s_servo[i].target_deg;
        }
        servo_write_pulse((Servo_Id)i);
    }
}

void Servo_Disable(Servo_Id id)
{
    if (id >= SERVO_COUNT) {
        return;
    }
    s_servo[id].active = false;
    servo_hw_stop_pulse(id);
}

bool Servo_IsActive(Servo_Id id)
{
    return (id < SERVO_COUNT) && s_servo[id].active;
}

float Servo_GetAngleDeg(Servo_Id id)
{
    if (id >= SERVO_COUNT) {
        return 0.0f;
    }
    return s_servo[id].current_deg;
}
