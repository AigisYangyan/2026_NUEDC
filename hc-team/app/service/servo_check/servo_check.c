/**
 * @file    servo_check.c
 * @brief   舵机测试/标定薄服务实现：操作员暂存角所有者（契约 §34 修订 1）。
 *
 * 菜单 RUN_ACTIVE 锁死约束下的工作流：空转期按钮改暂存角 → 进 ServoTest 施加 →
 * 观察 → BACK 释放 → 再改再进。空转期不触碰 driver（未 Init 的 driver 不收命令）。
 * 限幅/斜坡/换算仍全归 driver/servo（§8.2）。
 */
#include "app/service/servo_check/servo_check.h"

#include "driver/servo/servo.h"

#define SERVO_CHECK_DEFAULT_DEG 90

/* 操作员暂存角（本服务唯一所有者；菜单行读写它，不直通 driver）。 */
static int32_t s_staged_deg[2] = { SERVO_CHECK_DEFAULT_DEG, SERVO_CHECK_DEFAULT_DEG };
static bool    s_staged_valid[2];
static bool    s_session_active;

static void servo_check_apply(uint8_t idx)
{
    /* 暂存值=操作员先前的显式命令；播种直达语义在 driver（首命令安全位责任在操作员）。 */
    (void)Servo_SetTargetDeg((idx == 0u) ? SERVO_1 : SERVO_2, (float)s_staged_deg[idx]);
}

void ServoCheck_Start(void)
{
    uint8_t i;

    Servo_Init();               /* 复位到自由态（零脉冲） */
    s_session_active = true;
    for (i = 0u; i < 2u; i++) {
        if (s_staged_valid[i]) {
            servo_check_apply(i);   /* 进页即摆到已设角；从未设过则保持自由态 */
        }
    }
}

void ServoCheck_Update(uint32_t now_ms)
{
    Servo_Update(now_ms);
}

void ServoCheck_Stop(void)
{
    s_session_active = false;
    Servo_Disable(SERVO_1);
    Servo_Disable(SERVO_2);
}

static void servo_check_set(uint8_t idx, int32_t deg)
{
    s_staged_deg[idx] = deg;
    s_staged_valid[idx] = true;
    if (s_session_active) {
        servo_check_apply(idx); /* 会话中（未来框架若开放运行期改参）立即透传 */
    }
}

int32_t ServoCheck_GetS1Deg(void)
{
    return s_staged_deg[0];
}

void ServoCheck_SetS1Deg(int32_t deg)
{
    servo_check_set(0u, deg);
}

int32_t ServoCheck_GetS2Deg(void)
{
    return s_staged_deg[1];
}

void ServoCheck_SetS2Deg(int32_t deg)
{
    servo_check_set(1u, deg);
}
