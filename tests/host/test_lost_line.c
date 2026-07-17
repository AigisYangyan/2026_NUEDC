/**
 * @file    test_lost_line.c
 * @brief   Host tests for the lost-line recovery policy (S02 私有子模块).
 *
 * 语义（契约 §8.2）：丢线时按最近有效误差的方向输出固定回退误差；
 * 从未见线（或最后误差为 0）→ 回退误差 0 = 直行找线；累计丢线时长
 * 达到 timeout → 返回 false（放弃，调用方转 LOST）。
 */
#include "app/service/line_follow/lost_line.h"

#include <math.h>
#include <stdio.h>

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_FLOAT_NEAR(actual, expected, epsilon) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (double)(actual), (double)(expected), (double)(epsilon), \
               __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static int test_never_seen_line_recovers_straight(void)
{
    LostLine_T ctx;
    float e = 99.0f;

    LostLine_Init(&ctx, 27.0f, 500u);
    TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    TEST_ASSERT_FLOAT_NEAR(e, 0.0f, 1e-6f);
    printf("PASS: test_never_seen_line_recovers_straight\n");
    return 0;
}

static int test_recovery_uses_last_direction_right(void)
{
    LostLine_T ctx;
    float e = 0.0f;

    LostLine_Init(&ctx, 27.0f, 500u);
    LostLine_NoteValid(&ctx, 12.3f);
    TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    TEST_ASSERT_FLOAT_NEAR(e, 27.0f, 1e-6f);
    printf("PASS: test_recovery_uses_last_direction_right\n");
    return 0;
}

static int test_recovery_uses_last_direction_left(void)
{
    LostLine_T ctx;
    float e = 0.0f;

    LostLine_Init(&ctx, 27.0f, 500u);
    LostLine_NoteValid(&ctx, -3.0f);
    TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    TEST_ASSERT_FLOAT_NEAR(e, -27.0f, 1e-6f);
    printf("PASS: test_recovery_uses_last_direction_left\n");
    return 0;
}

static int test_zero_last_error_recovers_straight(void)
{
    LostLine_T ctx;
    float e = 99.0f;

    LostLine_Init(&ctx, 27.0f, 500u);
    LostLine_NoteValid(&ctx, 0.0f);
    TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    TEST_ASSERT_FLOAT_NEAR(e, 0.0f, 1e-6f);
    printf("PASS: test_zero_last_error_recovers_straight\n");
    return 0;
}

static int test_timeout_gives_up(void)
{
    LostLine_T ctx;
    float e = 0.0f;
    int i;

    LostLine_Init(&ctx, 27.0f, 100u);
    LostLine_NoteValid(&ctx, 5.0f);
    for (i = 0; i < 9; i++) { /* 累计 90ms：仍在恢复 */
        TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    }
    TEST_ASSERT_TRUE(!LostLine_Tick(&ctx, 10u, &e)); /* 累计 100ms：放弃 */
    printf("PASS: test_timeout_gives_up\n");
    return 0;
}

static int test_note_valid_resets_timer(void)
{
    LostLine_T ctx;
    float e = 0.0f;
    int i;

    LostLine_Init(&ctx, 27.0f, 100u);
    LostLine_NoteValid(&ctx, 5.0f);
    for (i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    }
    LostLine_NoteValid(&ctx, -5.0f); /* 重获线：计时清零、方向更新 */
    for (i = 0; i < 9; i++) {
        TEST_ASSERT_TRUE(LostLine_Tick(&ctx, 10u, &e));
    }
    TEST_ASSERT_FLOAT_NEAR(e, -27.0f, 1e-6f);
    printf("PASS: test_note_valid_resets_timer\n");
    return 0;
}

static int test_zero_timeout_gives_up_immediately(void)
{
    LostLine_T ctx;
    float e = 0.0f;

    LostLine_Init(&ctx, 27.0f, 0u);
    LostLine_NoteValid(&ctx, 5.0f);
    TEST_ASSERT_TRUE(!LostLine_Tick(&ctx, 10u, &e));
    printf("PASS: test_zero_timeout_gives_up_immediately\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_never_seen_line_recovers_straight();
    failures += test_recovery_uses_last_direction_right();
    failures += test_recovery_uses_last_direction_left();
    failures += test_zero_last_error_recovers_straight();
    failures += test_timeout_gives_up();
    failures += test_note_valid_resets_timer();
    failures += test_zero_timeout_gives_up_immediately();

    if (failures != 0) {
        printf("%d lost line test(s) failed.\n", failures);
        return 1;
    }
    printf("All lost line tests passed.\n");
    return 0;
}
