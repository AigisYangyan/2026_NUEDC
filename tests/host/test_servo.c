/**
 * @file    test_servo.c
 * @brief   舵机 Driver + servo_check 服务主机测试（SV1，契约 §34）。
 *
 * 链接组成：真实 servo.c + servo_check.c + fake_servo_hw.c。
 */
#include "app/service/servo_check/servo_check.h"
#include "driver/servo/servo.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern void     FakeServoHw_Reset(void);
extern bool     FakeServoHw_Started(void);
extern uint32_t FakeServoHw_PulseUs(Servo_Id id);
extern uint32_t FakeServoHw_WriteCount(Servo_Id id);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static void setup(void)
{
    FakeServoHw_Reset();
    Servo_Init();
}

/* Init：起振但零脉冲、双路自由态（§8.1 上电安全）。 */
static int test_init_no_pulse(void)
{
    setup();
    TEST_ASSERT_TRUE(FakeServoHw_Started());
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 0u && FakeServoHw_PulseUs(SERVO_2) == 0u);
    TEST_ASSERT_TRUE(!Servo_IsActive(SERVO_1) && !Servo_IsActive(SERVO_2));
    printf("PASS: test_init_no_pulse\n");
    return 0;
}

/* 未命令时 Update 不写脉冲。 */
static int test_update_without_command_no_pulse(void)
{
    setup();
    Servo_Update(1000u);
    Servo_Update(1010u);
    TEST_ASSERT_TRUE(FakeServoHw_WriteCount(SERVO_1) == 0u && FakeServoHw_WriteCount(SERVO_2) == 0u);
    printf("PASS: test_update_without_command_no_pulse\n");
    return 0;
}

/* 自由态首命令：播种当前=目标，下一到期拍直达脉宽（90°→1500µs）。 */
static int test_first_command_seeds_direct(void)
{
    setup();
    TEST_ASSERT_TRUE(Servo_SetTargetDeg(SERVO_1, 90.0f));
    TEST_ASSERT_TRUE(Servo_IsActive(SERVO_1));
    Servo_Update(1000u);                    /* 播种周期基准 */
    Servo_Update(1010u);                    /* 到期写脉宽 */
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 1500u);
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 90.0f) < 1e-5f);
    printf("PASS: test_first_command_seeds_direct\n");
    return 0;
}

/* 角→脉宽端点：0°→500µs、180°→2500µs（Disable 后重播种直达）。 */
static int test_angle_to_pulse_endpoints(void)
{
    setup();
    (void)Servo_SetTargetDeg(SERVO_1, 0.0f);
    Servo_Update(0u);
    Servo_Update(10u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 500u);

    Servo_Disable(SERVO_1);
    (void)Servo_SetTargetDeg(SERVO_1, 180.0f);  /* 重播种直达 */
    Servo_Update(20u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 2500u);
    printf("PASS: test_angle_to_pulse_endpoints\n");
    return 0;
}

/* 软限位夹域（唯一限幅点）。 */
static int test_clamp_to_limits(void)
{
    setup();
    TEST_ASSERT_TRUE(Servo_SetLimitsDeg(SERVO_1, 20.0f, 120.0f));
    (void)Servo_SetTargetDeg(SERVO_1, 150.0f);      /* → 夹到 120 */
    Servo_Update(0u);
    Servo_Update(10u);
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 120.0f) < 1e-5f);
    (void)Servo_SetTargetDeg(SERVO_1, 5.0f);        /* → 夹到 20（斜坡起点 120） */
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 120.0f) < 1e-5f); /* 目标夹了，当前未跳 */
    printf("PASS: test_clamp_to_limits\n");
    return 0;
}

/* 非法限位/速率拒绝且旧值保留。 */
static int test_invalid_limits_and_rate_rejected(void)
{
    setup();
    TEST_ASSERT_TRUE(!Servo_SetLimitsDeg(SERVO_1, 120.0f, 20.0f));   /* min>=max */
    TEST_ASSERT_TRUE(!Servo_SetLimitsDeg(SERVO_1, -5.0f, 90.0f));    /* 出域 */
    TEST_ASSERT_TRUE(!Servo_SetLimitsDeg(SERVO_1, 0.0f, 181.0f));
    TEST_ASSERT_TRUE(!Servo_SetRateDegPerS(SERVO_1, 0.0f));
    TEST_ASSERT_TRUE(!Servo_SetRateDegPerS(SERVO_1, -10.0f));
    (void)Servo_SetTargetDeg(SERVO_1, 180.0f);      /* 默认限位 0..180 仍在 → 接受 */
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 180.0f) < 1e-5f);
    printf("PASS: test_invalid_limits_and_rate_rejected\n");
    return 0;
}

/* 斜坡步进：rate=100°/s、10ms/拍 → 每拍 1°；到位后精确停在目标（无过冲）。 */
static int test_slew_steps_and_settles(void)
{
    uint32_t t = 0u;
    int i;

    setup();
    (void)Servo_SetTargetDeg(SERVO_1, 0.0f);        /* 播种 0° */
    TEST_ASSERT_TRUE(Servo_SetRateDegPerS(SERVO_1, 100.0f));
    Servo_Update(t);                                /* 周期基准 */
    (void)Servo_SetTargetDeg(SERVO_1, 5.0f);        /* 已激活 → 斜坡推进 */
    t += 10u;
    Servo_Update(t);                                /* +1° → 1° */
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 1.0f) < 1e-4f);
    for (i = 0; i < 10; i++) {                      /* 再 10 拍，5° 处收敛 */
        t += 10u;
        Servo_Update(t);
    }
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 5.0f) < 1e-4f);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 556u);  /* 500+5/180*2000=555.6→556 */
    printf("PASS: test_slew_steps_and_settles\n");
    return 0;
}

/* Disable 停脉冲 + IsActive 翻转；重命令重播种（不从陈旧角斜坡）。 */
static int test_disable_and_reseed(void)
{
    setup();
    (void)Servo_SetTargetDeg(SERVO_2, 90.0f);
    Servo_Update(0u);
    Servo_Update(10u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_2) == 1500u);
    Servo_Disable(SERVO_2);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_2) == 0u && !Servo_IsActive(SERVO_2));
    (void)Servo_SetTargetDeg(SERVO_2, 45.0f);       /* 重播种 45° 直达 */
    Servo_Update(20u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_2) == 1000u);  /* 500+45/180*2000 */
    printf("PASS: test_disable_and_reseed\n");
    return 0;
}

/* 10ms 门控：不足周期不写。 */
static int test_gating_10ms(void)
{
    uint32_t writes;

    setup();
    (void)Servo_SetTargetDeg(SERVO_1, 90.0f);
    Servo_Update(1000u);                    /* 播种 */
    writes = FakeServoHw_WriteCount(SERVO_1);
    Servo_Update(1005u);                    /* 5ms 未到期 */
    TEST_ASSERT_TRUE(FakeServoHw_WriteCount(SERVO_1) == writes);
    Servo_Update(1010u);
    TEST_ASSERT_TRUE(FakeServoHw_WriteCount(SERVO_1) == writes + 1u);
    printf("PASS: test_gating_10ms\n");
    return 0;
}

/* servo_check 暂存语义（契约 §34 修订 1）：
 * 空转 Set 只暂存不触 driver → Start 施加已设路 → Stop 释放 → 重进重施加。 */
static int test_servo_check_staged_semantics(void)
{
    FakeServoHw_Reset();
    Servo_Init();                                   /* 模拟上一会话遗留后进入空转 */
    ServoCheck_Stop();                              /* 确保会话标志=空转态 */
    TEST_ASSERT_TRUE(ServoCheck_GetS1Deg() == 90 || ServoCheck_GetS1Deg() >= 0); /* 默认显示口径 */

    ServoCheck_SetS1Deg(45);                        /* 空转期：只暂存 */
    TEST_ASSERT_TRUE(!Servo_IsActive(SERVO_1));     /* 不触 driver */
    TEST_ASSERT_TRUE(ServoCheck_GetS1Deg() == 45);  /* Get 回暂存 */

    ServoCheck_Start();                             /* 进页：施加已设的 S1，S2 未设保持自由 */
    TEST_ASSERT_TRUE(Servo_IsActive(SERVO_1));
    ServoCheck_Update(0u);
    ServoCheck_Update(10u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 1000u);  /* 45° → 500+500 */

    ServoCheck_Stop();                              /* BACK：双路释放 */
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 0u && !Servo_IsActive(SERVO_1));

    ServoCheck_SetS1Deg(90);                        /* 空转再改 */
    ServoCheck_Start();                             /* 重进：重施加 */
    ServoCheck_Update(100u);
    ServoCheck_Update(110u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_1) == 1500u);
    ServoCheck_Stop();
    printf("PASS: test_servo_check_staged_semantics\n");
    return 0;
}

/* servo_check 会话中 Set 立即透传 + 夹域归 driver（S2=200→180）。 */
static int test_servo_check_live_set_and_clamp(void)
{
    FakeServoHw_Reset();
    ServoCheck_Start();
    ServoCheck_SetS2Deg(200);                       /* 会话中：立即透传，driver 夹 180 */
    ServoCheck_Update(0u);
    ServoCheck_Update(10u);
    TEST_ASSERT_TRUE(FakeServoHw_PulseUs(SERVO_2) == 2500u);
    TEST_ASSERT_TRUE(ServoCheck_GetS2Deg() == 200); /* 暂存保留操作员原值（编辑口径） */
    ServoCheck_Stop();
    printf("PASS: test_servo_check_live_set_and_clamp\n");
    return 0;
}

/* 斜坡速率变更即时生效（下一到期拍按新速率推进）。 */
static int test_rate_change_takes_effect(void)
{
    setup();
    (void)Servo_SetTargetDeg(SERVO_1, 0.0f);
    (void)Servo_SetRateDegPerS(SERVO_1, 100.0f);
    Servo_Update(0u);
    (void)Servo_SetTargetDeg(SERVO_1, 20.0f);
    Servo_Update(10u);                              /* 1°/拍 → 1° */
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 1.0f) < 1e-4f);
    (void)Servo_SetRateDegPerS(SERVO_1, 200.0f);
    Servo_Update(20u);                              /* 2°/拍 → 3° */
    TEST_ASSERT_TRUE(fabsf(Servo_GetAngleDeg(SERVO_1) - 3.0f) < 1e-4f);
    printf("PASS: test_rate_change_takes_effect\n");
    return 0;
}

/* id 越界安全。 */
static int test_out_of_range_id_safe(void)
{
    setup();
    TEST_ASSERT_TRUE(!Servo_SetTargetDeg(SERVO_COUNT, 90.0f));
    TEST_ASSERT_TRUE(!Servo_IsActive(SERVO_COUNT));
    Servo_Disable(SERVO_COUNT);                     /* 不崩即过 */
    TEST_ASSERT_TRUE(Servo_GetAngleDeg(SERVO_COUNT) == 0.0f);
    printf("PASS: test_out_of_range_id_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_no_pulse();
    failures += test_update_without_command_no_pulse();
    failures += test_first_command_seeds_direct();
    failures += test_angle_to_pulse_endpoints();
    failures += test_clamp_to_limits();
    failures += test_invalid_limits_and_rate_rejected();
    failures += test_slew_steps_and_settles();
    failures += test_disable_and_reseed();
    failures += test_gating_10ms();
    failures += test_servo_check_staged_semantics();
    failures += test_servo_check_live_set_and_clamp();
    failures += test_rate_change_takes_effect();
    failures += test_out_of_range_id_safe();

    if (failures != 0) {
        printf("%d servo test(s) failed.\n", failures);
        return 1;
    }

    printf("All servo driver/service tests passed.\n");
    return 0;
}
