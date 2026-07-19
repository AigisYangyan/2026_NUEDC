/**
 * @file    test_param_tune.c
 * @brief   按钮动态调参持久化服务主机测试（PT2，契约 §25.3）。
 *
 * 链接组成：真实 param_tune.c + line_follow.c(+全依赖) + param_store.c
 *           + fake_param_store_port.c（RAM 顶替 flash）+ line_follow 既有 fake 集。
 * 验：默认载入、持久载入、set 即时生效并抵达 line_follow、Save 往返恢复、milli↔float scale、负值。
 */
#include "app/service/param_tune/param_tune.h"

#include "app/service/line_follow/line_follow.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

extern void FakeParamStorePort_Reset(void);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 把 line_follow 增益置到已知非零态（不经 flash），用于「模拟掉电丢失」前后对照。 */
static void force_gains_milli(int32_t kp, int32_t ki, int32_t kd)
{
    ParamTune_SetKp_milli(kp);
    ParamTune_SetKi_milli(ki);
    ParamTune_SetKd_milli(kd);
}

/* 空 flash → Init 应用默认增益（0），三增益读回=0。 */
static int test_init_blank_applies_default(void)
{
    FakeParamStorePort_Reset();
    force_gains_milli(999, 888, 777);      /* 先污染，证 Init 会覆盖 */
    ParamTune_Init();
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 0);
    TEST_ASSERT_TRUE(ParamTune_GetKi_milli() == 0);
    TEST_ASSERT_TRUE(ParamTune_GetKd_milli() == 0);
    printf("PASS: test_init_blank_applies_default\n");
    return 0;
}

/* Set 即时生效并抵达 line_follow（GetGains 读回 float ≈ milli/1000），无需 Save。 */
static int test_set_applies_to_line_follow_immediately(void)
{
    float kp, ki, kd;

    FakeParamStorePort_Reset();
    ParamTune_SetKp_milli(1234);
    /* param_tune 读回一致 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 1234);
    /* 真正抵达 line_follow 外环 PID */
    LineFollow_GetGains(&kp, &ki, &kd);
    TEST_ASSERT_TRUE(fabsf(kp - 1.234f) < 1e-4f);
    printf("PASS: test_set_applies_to_line_follow_immediately\n");
    return 0;
}

/* Save → 模拟掉电（改成别的值）→ Init 从 flash 恢复保存值（非默认、非被改值）。 */
static int test_save_then_init_restores(void)
{
    FakeParamStorePort_Reset();
    force_gains_milli(1500, 200, 750);
    ParamTune_Save();                      /* 存 1500/200/750 */

    force_gains_milli(0, 0, 0);            /* 模拟掉电后 line_follow 增益丢失 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 0);

    ParamTune_Init();                      /* 从 flash 恢复 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 1500);
    TEST_ASSERT_TRUE(ParamTune_GetKi_milli() == 200);
    TEST_ASSERT_TRUE(ParamTune_GetKd_milli() == 750);
    printf("PASS: test_save_then_init_restores\n");
    return 0;
}

/* set 后未 Save，Init 仍回 flash 旧值（证 set 只改 RAM、Save 才落盘）。 */
static int test_unsaved_set_not_persisted(void)
{
    FakeParamStorePort_Reset();
    force_gains_milli(300, 0, 100);
    ParamTune_Save();                      /* flash = 300/0/100 */

    ParamTune_SetKp_milli(9999);          /* 只改 RAM，不存 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 9999);

    ParamTune_Init();                      /* 回 flash 值 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 300);
    printf("PASS: test_unsaved_set_not_persisted\n");
    return 0;
}

/* milli↔float scale 往返无损（正/负）：设 milli 读回同 milli。 */
static int test_scale_roundtrip_signed(void)
{
    float kp, ki, kd;

    FakeParamStorePort_Reset();
    ParamTune_SetKp_milli(2500);          /* 2.500 */
    ParamTune_SetKi_milli(-50);           /* -0.050 */
    ParamTune_SetKd_milli(1);             /* 0.001 最小分辨率 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 2500);
    TEST_ASSERT_TRUE(ParamTune_GetKi_milli() == -50);
    TEST_ASSERT_TRUE(ParamTune_GetKd_milli() == 1);

    LineFollow_GetGains(&kp, &ki, &kd);
    TEST_ASSERT_TRUE(fabsf(ki - (-0.050f)) < 1e-4f);
    printf("PASS: test_scale_roundtrip_signed\n");
    return 0;
}

/* set 单增益不动另两增益（保值语义）。 */
static int test_set_one_preserves_others(void)
{
    FakeParamStorePort_Reset();
    force_gains_milli(100, 200, 300);
    ParamTune_SetKp_milli(111);
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 111);
    TEST_ASSERT_TRUE(ParamTune_GetKi_milli() == 200);   /* 不动 */
    TEST_ASSERT_TRUE(ParamTune_GetKd_milli() == 300);   /* 不动 */
    printf("PASS: test_set_one_preserves_others\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_blank_applies_default();
    failures += test_set_applies_to_line_follow_immediately();
    failures += test_save_then_init_restores();
    failures += test_unsaved_set_not_persisted();
    failures += test_scale_roundtrip_signed();
    failures += test_set_one_preserves_others();

    if (failures != 0) {
        printf("param_tune service tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll param_tune service tests passed.\n");
    return 0;
}
