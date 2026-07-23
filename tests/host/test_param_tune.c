/**
 * @file    test_param_tune.c
 * @brief   按钮动态调参持久化服务主机测试（PT2，契约 §25.3）。
 *
 * 链接组成：真实 param_tune.c + line_follow.c(+全依赖) + param_store.c
 *           + fake_param_store_port.c（RAM 顶替 flash）+ line_follow 既有 fake 集。
 * 验：默认载入、持久载入、set 即时生效并抵达 line_follow、Save 往返恢复、milli↔float scale、负值。
 */
#include "app/service/param_tune/param_tune.h"

#include "app/service/chassis/chassis.h"
#include "app/service/line_follow/line_follow.h"
#include "app/service/motion/motion.h"
#include "driver/param_store/param_store.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

extern void FakeParamStorePort_Reset(void);

/* imu.c 写寄存器路径引用 Mspm0Runtime_DelayMs（本测试从不调用 motion 采样路径，但链接 imu.o
 * 需该符号）；提供空桩满足链接（同 test_motion.c 做法）。 */
void Mspm0Runtime_DelayMs(uint32_t delay_ms)
{
    (void)delay_ms;
}

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

/* milli↔float scale 往返无损（正域）+ 负值落 0（§37.7 改约：负增益=第二方向开关，禁）。 */
static int test_scale_roundtrip_signed(void)
{
    float kp, ki, kd;

    FakeParamStorePort_Reset();
    ParamTune_SetKp_milli(2500);          /* 2.500 */
    ParamTune_SetKi_milli(-50);           /* 负值 → 清洗落 0（原契约允许负，§37.7 改约） */
    ParamTune_SetKd_milli(1);             /* 0.001 最小分辨率 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 2500);
    TEST_ASSERT_TRUE(ParamTune_GetKi_milli() == 0);
    TEST_ASSERT_TRUE(ParamTune_GetKd_milli() == 1);

    LineFollow_GetGains(&kp, &ki, &kd);
    TEST_ASSERT_TRUE(fabsf(ki - 0.0f) < 1e-4f);
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

/* ---- DRIVE 组：motion 剖面参数 + 测试距离（§28）------------------------- */

/* 空 flash → Init 应用剖面/距离默认（cruise 200/start 80/accel 300/decel 300/dist 1000）。 */
static int test_init_blank_applies_profile_dist_defaults(void)
{
    FakeParamStorePort_Reset();
    ParamTune_Init();
    TEST_ASSERT_TRUE(ParamTune_GetCruise_milli() == 200);
    TEST_ASSERT_TRUE(ParamTune_GetStart_milli() == 80);
    TEST_ASSERT_TRUE(ParamTune_GetAccel_milli() == 300);
    TEST_ASSERT_TRUE(ParamTune_GetDecel_milli() == 300);
    TEST_ASSERT_TRUE(ParamTune_GetDist_mm() == 1000);
    printf("PASS: test_init_blank_applies_profile_dist_defaults\n");
    return 0;
}

/* SetCruise 即时抵达 motion（Motion_GetProfileParams 读回 ≈ milli/1000）；保另三参数。 */
static int test_profile_set_reaches_motion(void)
{
    float cruise, start, accel, decel;

    FakeParamStorePort_Reset();
    ParamTune_Init();                     /* 默认态 */
    ParamTune_SetCruise_milli(500);       /* 0.500 */
    TEST_ASSERT_TRUE(ParamTune_GetCruise_milli() == 500);
    Motion_GetProfileParams(&cruise, &start, &accel, &decel);
    TEST_ASSERT_TRUE(fabsf(cruise - 0.500f) < 1e-4f);
    TEST_ASSERT_TRUE(ParamTune_GetStart_milli() == 80);   /* 保值：另三不动 */
    TEST_ASSERT_TRUE(ParamTune_GetAccel_milli() == 300);
    TEST_ASSERT_TRUE(ParamTune_GetDecel_milli() == 300);
    printf("PASS: test_profile_set_reaches_motion\n");
    return 0;
}

/* 测试距离由 param_tune 自持：Set→Get 一致。 */
static int test_dist_selfheld_set_get(void)
{
    FakeParamStorePort_Reset();
    ParamTune_SetDist_mm(1500);
    TEST_ASSERT_TRUE(ParamTune_GetDist_mm() == 1500);
    printf("PASS: test_dist_selfheld_set_get\n");
    return 0;
}

/* Save → 掉电改值 → Init 从 flash 恢复剖面 4 参数 + 距离（schema_ver 2 全量往返）。 */
static int test_save_restores_profile_and_dist(void)
{
    FakeParamStorePort_Reset();
    ParamTune_Init();
    ParamTune_SetCruise_milli(650);
    ParamTune_SetStart_milli(120);
    ParamTune_SetAccel_milli(450);
    ParamTune_SetDecel_milli(550);
    ParamTune_SetDist_mm(1800);
    ParamTune_Save();                     /* 存全量 */

    ParamTune_SetCruise_milli(0);        /* 模拟掉电改值 */
    ParamTune_SetDist_mm(0);

    ParamTune_Init();                     /* 从 flash 恢复 */
    TEST_ASSERT_TRUE(ParamTune_GetCruise_milli() == 650);
    TEST_ASSERT_TRUE(ParamTune_GetStart_milli() == 120);
    TEST_ASSERT_TRUE(ParamTune_GetAccel_milli() == 450);
    TEST_ASSERT_TRUE(ParamTune_GetDecel_milli() == 550);
    TEST_ASSERT_TRUE(ParamTune_GetDist_mm() == 1800);
    printf("PASS: test_save_restores_profile_and_dist\n");
    return 0;
}

/* 向后：旧 13B(schema_ver 1) 记录经 Read(len=33) 长度不符被忽略 → 全默认（一次性丢旧增益）。 */
static int test_old_v1_blob_ignored_uses_defaults(void)
{
    uint8_t v1[13];
    int i;

    FakeParamStorePort_Reset();
    v1[0] = 1u;                            /* 旧 schema_ver=1 */
    for (i = 1; i < 13; i++) {
        v1[i] = 0xAAu;                     /* 任意旧增益字节 */
    }
    TEST_ASSERT_TRUE(ParamStore_Save(v1, 13u));   /* 写一条 13B 旧记录 */

    ParamTune_Init();                      /* 读 len=33 → 长度不符 → 全默认 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 0);         /* LF 增益回默认 */
    TEST_ASSERT_TRUE(ParamTune_GetCruise_milli() == 200);   /* 剖面默认 */
    TEST_ASSERT_TRUE(ParamTune_GetDist_mm() == 1000);       /* 距离默认 */
    printf("PASS: test_old_v1_blob_ignored_uses_defaults\n");
    return 0;
}

/* PT3v：底盘增益 Set 即达 chassis 唯一所有者，且双轮同值。 */
static int test_chassis_set_reaches_owner_both_sides(void)
{
    float lkp, lki, lkd, rkp, rki, rkd;

    FakeParamStorePort_Reset();
    ParamTune_Init();
    ParamTune_SetCKp_milli(2200);
    ParamTune_SetCKi_milli(340);
    ParamTune_SetCKd_milli(50);
    Chassis_GetSpeedGains(CHASSIS_SIDE_LEFT, &lkp, &lki, &lkd);
    Chassis_GetSpeedGains(CHASSIS_SIDE_RIGHT, &rkp, &rki, &rkd);
    TEST_ASSERT_TRUE(fabsf(lkp - 2.2f) < 1e-4f && fabsf(rkp - 2.2f) < 1e-4f);
    TEST_ASSERT_TRUE(fabsf(lki - 0.34f) < 1e-4f && fabsf(rki - 0.34f) < 1e-4f);
    TEST_ASSERT_TRUE(fabsf(lkd - 0.05f) < 1e-4f && fabsf(rkd - 0.05f) < 1e-4f);
    TEST_ASSERT_TRUE(ParamTune_GetCKp_milli() == 2200);
    printf("PASS: test_chassis_set_reaches_owner_both_sides\n");
    return 0;
}

/* PT3v：航向调参 Set 即达 motion 唯一所有者。 */
static int test_heading_set_reaches_motion(void)
{
    float hkp, hki, hkd, htkp;

    FakeParamStorePort_Reset();
    ParamTune_Init();
    ParamTune_SetHKp_milli(1500);
    ParamTune_SetHKi_milli(0);
    ParamTune_SetHKd_milli(250);
    ParamTune_SetHTKp_milli(45);
    Motion_GetHeadingTuning(&hkp, &hki, &hkd, &htkp);
    TEST_ASSERT_TRUE(fabsf(hkp - 1.5f) < 1e-4f);
    TEST_ASSERT_TRUE(fabsf(hkd - 0.25f) < 1e-4f);
    TEST_ASSERT_TRUE(fabsf(htkp - 0.045f) < 1e-4f);
    TEST_ASSERT_TRUE(ParamTune_GetHTKp_milli() == 45);
    printf("PASS: test_heading_set_reaches_motion\n");
    return 0;
}

/* PT3v：转弯测试角自持 + 空 flash 默认 90。 */
static int test_turn_deg_selfheld_default(void)
{
    FakeParamStorePort_Reset();
    ParamTune_SetTurnDeg(135);
    TEST_ASSERT_TRUE(ParamTune_GetTurnDeg() == 135);
    ParamTune_Init();                      /* 空 flash → 回默认 */
    TEST_ASSERT_TRUE(ParamTune_GetTurnDeg() == 90);
    TEST_ASSERT_TRUE(ParamTune_GetCKp_milli() == 0);    /* 底盘/航向默认 0（不拍系数） */
    TEST_ASSERT_TRUE(ParamTune_GetHKp_milli() == 0);
    printf("PASS: test_turn_deg_selfheld_default\n");
    return 0;
}

/* PT3v：v3 全量往返——新 8 字段 Save→掉电→Init 恢复。 */
static int test_v3_save_restores_new_fields(void)
{
    FakeParamStorePort_Reset();
    ParamTune_Init();
    ParamTune_SetCKp_milli(1800);
    ParamTune_SetCKi_milli(260);
    ParamTune_SetCKd_milli(0);
    ParamTune_SetHKp_milli(900);
    ParamTune_SetHKi_milli(10);
    ParamTune_SetHKd_milli(120);
    ParamTune_SetHTKp_milli(60);
    ParamTune_SetTurnDeg(180);
    ParamTune_Save();

    ParamTune_SetCKp_milli(0);            /* 模拟掉电改值 */
    ParamTune_SetHTKp_milli(0);
    ParamTune_SetTurnDeg(0);

    ParamTune_Init();
    TEST_ASSERT_TRUE(ParamTune_GetCKp_milli() == 1800);
    TEST_ASSERT_TRUE(ParamTune_GetCKi_milli() == 260);
    TEST_ASSERT_TRUE(ParamTune_GetHKp_milli() == 900);
    TEST_ASSERT_TRUE(ParamTune_GetHKi_milli() == 10);
    TEST_ASSERT_TRUE(ParamTune_GetHKd_milli() == 120);
    TEST_ASSERT_TRUE(ParamTune_GetHTKp_milli() == 60);
    TEST_ASSERT_TRUE(ParamTune_GetTurnDeg() == 180);
    printf("PASS: test_v3_save_restores_new_fields\n");
    return 0;
}

/* PT3v：旧 v2(33B) 记录经 Read(len=65) 长度不符被忽略 → 全默认（一次性失效先例）。 */
static int test_old_v2_blob_ignored_uses_defaults(void)
{
    uint8_t v2[33];
    int i;

    FakeParamStorePort_Reset();
    v2[0] = 2u;                            /* 旧 schema_ver=2 */
    for (i = 1; i < 33; i++) {
        v2[i] = 0x55u;
    }
    TEST_ASSERT_TRUE(ParamStore_Save(v2, 33u));

    ParamTune_Init();                      /* 读 len=65 → 长度不符 → 全默认 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 0);
    TEST_ASSERT_TRUE(ParamTune_GetCKp_milli() == 0);
    TEST_ASSERT_TRUE(ParamTune_GetTurnDeg() == 90);
    printf("PASS: test_old_v2_blob_ignored_uses_defaults\n");
    return 0;
}

/* §37.7：负增益从任何入口（UI setter / flash 恢复路径共用 apply）一律落 0。 */
static int test_negative_gain_clamped_to_zero(void)
{
    FakeParamStorePort_Reset();
    ParamTune_Init();
    ParamTune_SetKp_milli(-5);            /* LF 直通 setter 路 */
    TEST_ASSERT_TRUE(ParamTune_GetKp_milli() == 0);
    ParamTune_SetCKp_milli(-100000);      /* chassis apply 路（负 Kp=正反馈飞车，重点封） */
    TEST_ASSERT_TRUE(ParamTune_GetCKp_milli() == 0);
    ParamTune_SetHTKp_milli(-1);          /* heading apply 路 */
    TEST_ASSERT_TRUE(ParamTune_GetHTKp_milli() == 0);
    ParamTune_SetTurnDeg(-90);            /* Ang 保留符号（反向转是合法语义） */
    TEST_ASSERT_TRUE(ParamTune_GetTurnDeg() == -90);
    printf("PASS: test_negative_gain_clamped_to_zero\n");
    return 0;
}

/* PT3v：单设一项保持其余（chassis/heading 组内独立性）。 */
static int test_new_set_one_preserves_others(void)
{
    FakeParamStorePort_Reset();
    ParamTune_Init();
    ParamTune_SetCKp_milli(1000);
    ParamTune_SetCKi_milli(200);
    ParamTune_SetCKp_milli(1100);         /* 只动 Kp */
    TEST_ASSERT_TRUE(ParamTune_GetCKi_milli() == 200);
    ParamTune_SetHKp_milli(700);
    ParamTune_SetHTKp_milli(30);
    ParamTune_SetHKp_milli(800);          /* 只动 HKp */
    TEST_ASSERT_TRUE(ParamTune_GetHTKp_milli() == 30);
    printf("PASS: test_new_set_one_preserves_others\n");
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
    failures += test_init_blank_applies_profile_dist_defaults();
    failures += test_profile_set_reaches_motion();
    failures += test_dist_selfheld_set_get();
    failures += test_save_restores_profile_and_dist();
    failures += test_old_v1_blob_ignored_uses_defaults();
    failures += test_chassis_set_reaches_owner_both_sides();
    failures += test_heading_set_reaches_motion();
    failures += test_turn_deg_selfheld_default();
    failures += test_v3_save_restores_new_fields();
    failures += test_old_v2_blob_ignored_uses_defaults();
    failures += test_negative_gain_clamped_to_zero();
    failures += test_new_set_one_preserves_others();

    if (failures != 0) {
        printf("param_tune service tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll param_tune service tests passed.\n");
    return 0;
}
