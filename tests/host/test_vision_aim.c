/**
 * @file    test_vision_aim.c
 * @brief   Host tests for vision_aim (S05b, 契约 §21.2).
 *
 * 约定回顾：
 * - error_px = coord − center（float，不截断；coord 大于 center → 正，即右/下为正）。
 * - 死区：|error|<=deadband → delta 0 / active false；越死区 → |delta| ∈ [1, max_step]（floor 1）。
 * - 极性：按 error 符号取正负，再乘 sign[axis]（唯一极性开关）。
 * - 轴程限幅：依 cur_pulse，令 cur+delta 不越 ±travel_limit；travel_limit<=0 不限幅。
 * - 双轴各用各自 config；纯函数不跨拍记账。
 *
 * 注意：vision_aim 配置是模块私有 static 且无 de-init，故「未 Init 即 Map」用例必须最先跑。
 */
#include "middleware/vision_aim/vision_aim.h"

#include <math.h>
#include <stdio.h>

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_NEAR(actual, expected, eps) do { \
    double _a = (double)(actual); \
    double _e = (double)(expected); \
    if (fabs(_a - _e) > (double)(eps)) { \
        printf("FAIL: %s ~= %g, expected %g at %s:%d\n", #actual, _a, _e, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define EPS 1e-4f

/* 基准配置：中心 320/240、死区 6px、kp 0.15、步长上限 48、极性 +1、轴程 X±800 Y±400。 */
static VisionAim_Config_T base_cfg(void)
{
    VisionAim_Config_T c;
    c.center_px[VISION_AIM_AXIS_X] = 320.0f;
    c.center_px[VISION_AIM_AXIS_Y] = 240.0f;
    c.deadband_px[VISION_AIM_AXIS_X] = 6.0f;
    c.deadband_px[VISION_AIM_AXIS_Y] = 6.0f;
    c.kp[VISION_AIM_AXIS_X] = 0.15f;
    c.kp[VISION_AIM_AXIS_Y] = 0.15f;
    c.max_step_pulse[VISION_AIM_AXIS_X] = 48;
    c.max_step_pulse[VISION_AIM_AXIS_Y] = 48;
    c.sign[VISION_AIM_AXIS_X] = 1;
    c.sign[VISION_AIM_AXIS_Y] = 1;
    c.travel_limit_pulse[VISION_AIM_AXIS_X] = 800;
    c.travel_limit_pulse[VISION_AIM_AXIS_Y] = 400;
    return c;
}

/* ---- 必须最先跑：未 Init 即 Map 不写出、不崩 --------------------------- */

static int test_map_before_init_no_write(void)
{
    VisionAim_Result_T r;
    r.delta_pulse[VISION_AIM_AXIS_X] = 12345;
    r.delta_pulse[VISION_AIM_AXIS_Y] = -6789;
    r.active[VISION_AIM_AXIS_X] = true;
    r.error_px[VISION_AIM_AXIS_X] = 111.0f;

    /* 无配置：Map 直接返回，不改 out */
    VisionAim_Map(1000.0f, 1000.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 12345);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_Y] == -6789);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == true);

    /* Init(NULL) 也不该建立配置 */
    VisionAim_Init(NULL);
    VisionAim_Map(1000.0f, 1000.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 12345);

    printf("PASS: test_map_before_init_no_write\n");
    return 0;
}

/* ---- 死区 -------------------------------------------------------------- */

static int test_deadband_holds(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* err_x = 325-320 = 5 (<6)；err_y = 240-240 = 0 */
    VisionAim_Map(325.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == false);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 0);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_Y] == false);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_Y] == 0);
    printf("PASS: test_deadband_holds\n");
    return 0;
}

static int test_deadband_boundary_exact_is_inside(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* err_x = 326-320 = 6，恰等于死区 → 判为界内（<=），不动 */
    VisionAim_Map(326.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == false);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 0);
    printf("PASS: test_deadband_boundary_exact_is_inside\n");
    return 0;
}

static int test_floor_one_forces_min_step(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    /* 小 kp 使 |err|*kp < 1：err=7 (>死区6)，7*0.05=0.35 → floor 到 1 → delta 1（杜绝触发不动） */
    cfg.kp[VISION_AIM_AXIS_X] = 0.05f;
    VisionAim_Init(&cfg);
    VisionAim_Map(327.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == true);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 1);
    printf("PASS: test_floor_one_forces_min_step\n");
    return 0;
}

/* ---- 比例 + 步长限幅 --------------------------------------------------- */

static int test_proportional_mid(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* err_x = 420-320 = 100 → 100*0.15 = 15 → delta 15 */
    VisionAim_Map(420.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == true);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 15);
    printf("PASS: test_proportional_mid\n");
    return 0;
}

static int test_max_step_clamp(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* err_x = 1320-320 = 1000 → 1000*0.15 = 150 > 48 → 封顶 48 */
    VisionAim_Map(1320.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 48);
    printf("PASS: test_max_step_clamp\n");
    return 0;
}

/* ---- 极性 -------------------------------------------------------------- */

static int test_error_sign_right_down_positive(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* coord 大于 center → error 正；delta 正（sign +1） */
    VisionAim_Map(420.0f, 340.0f, 0, 0, &r);
    TEST_ASSERT_NEAR(r.error_px[VISION_AIM_AXIS_X], 100.0f, EPS);
    TEST_ASSERT_NEAR(r.error_px[VISION_AIM_AXIS_Y], 100.0f, EPS);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] > 0);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_Y] > 0);
    printf("PASS: test_error_sign_right_down_positive\n");
    return 0;
}

static int test_negative_error_negative_delta(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* err_x = 220-320 = -100 → delta = -15 */
    VisionAim_Map(220.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_NEAR(r.error_px[VISION_AIM_AXIS_X], -100.0f, EPS);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == -15);
    printf("PASS: test_negative_error_negative_delta\n");
    return 0;
}

static int test_sign_inverts_direction(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    /* 极性反转：同一正误差 → 负 delta */
    cfg.sign[VISION_AIM_AXIS_X] = -1;
    VisionAim_Init(&cfg);
    VisionAim_Map(420.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_NEAR(r.error_px[VISION_AIM_AXIS_X], 100.0f, EPS);   /* 误差符号不受 sign 影响 */
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == -15);      /* 输出方向被 sign 反转 */
    printf("PASS: test_sign_inverts_direction\n");
    return 0;
}

/* ---- 轴程限幅 ---------------------------------------------------------- */

static int test_travel_clamp_upper(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* delta 本应 +15，但 cur=790、limit=800 → next=805 越界 → 截到 800-790 = 10 */
    VisionAim_Map(420.0f, 240.0f, 790, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 10);
    printf("PASS: test_travel_clamp_upper\n");
    return 0;
}

static int test_travel_clamp_lower(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* err 负、delta 本应 -15，但 cur=-790、limit=800 → next=-805 越界 → 截到 -800-(-790) = -10 */
    VisionAim_Map(220.0f, 240.0f, -790, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == -10);
    printf("PASS: test_travel_clamp_lower\n");
    return 0;
}

static int test_travel_within_unclamped(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    VisionAim_Init(&cfg);
    /* cur=0，delta +15，limit 800 → 不截 */
    VisionAim_Map(420.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 15);
    printf("PASS: test_travel_within_unclamped\n");
    return 0;
}

static int test_travel_limit_zero_disables_clamp(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    /* travel_limit <=0 → 不限幅：cur 极大也不截 */
    cfg.travel_limit_pulse[VISION_AIM_AXIS_X] = 0;
    VisionAim_Init(&cfg);
    VisionAim_Map(420.0f, 240.0f, 1000000, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 15);
    printf("PASS: test_travel_limit_zero_disables_clamp\n");
    return 0;
}

/* ---- 双轴独立 --------------------------------------------------------- */

static int test_axes_independent(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    /* X/Y 各不同 kp/极性；X 大误差动，Y 在死区内不动 */
    cfg.kp[VISION_AIM_AXIS_Y] = 0.30f;
    cfg.sign[VISION_AIM_AXIS_Y] = -1;
    VisionAim_Init(&cfg);
    /* err_x = 420-320 = 100 → 15；err_y = 243-240 = 3 (<6) → 0 */
    VisionAim_Map(420.0f, 243.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 15);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == true);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_Y] == 0);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_Y] == false);

    /* 现在让 Y 动：err_y = 340-240 = 100 → 100*0.30 = 30，sign −1 → -30；X 同上 15 不变 */
    VisionAim_Map(420.0f, 340.0f, 0, 0, &r);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_X] == 15);
    TEST_ASSERT_TRUE(r.delta_pulse[VISION_AIM_AXIS_Y] == -30);
    printf("PASS: test_axes_independent\n");
    return 0;
}

/* ---- float 精度：亚像素中心不被截断 ----------------------------------- */

static int test_subpixel_center_not_truncated(void)
{
    VisionAim_Config_T cfg = base_cfg();
    VisionAim_Result_T r;

    /* 亚像素中心 319.5，coord 320.0 → err = 0.5（float 保留）。
     * 旧代码先 (int32)coord 再减 (int)center，会得 320-319=1（早失精度）——此处证明修复。 */
    cfg.center_px[VISION_AIM_AXIS_X] = 319.5f;
    cfg.deadband_px[VISION_AIM_AXIS_X] = 0.1f;   /* 缩小死区好让 0.5 越界 */
    VisionAim_Init(&cfg);
    VisionAim_Map(320.0f, 240.0f, 0, 0, &r);
    TEST_ASSERT_NEAR(r.error_px[VISION_AIM_AXIS_X], 0.5f, EPS);
    TEST_ASSERT_TRUE(r.active[VISION_AIM_AXIS_X] == true);
    printf("PASS: test_subpixel_center_not_truncated\n");
    return 0;
}

/* ---- NULL out 安全 ---------------------------------------------------- */

static int test_null_out_safe(void)
{
    VisionAim_Config_T cfg = base_cfg();

    VisionAim_Init(&cfg);
    VisionAim_Map(420.0f, 340.0f, 0, 0, NULL);   /* 不崩即通过 */
    printf("PASS: test_null_out_safe\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    /* 必须最先：模块 static 配置无 de-init，此后任一 Init 会永久建立配置 */
    failures += test_map_before_init_no_write();

    failures += test_deadband_holds();
    failures += test_deadband_boundary_exact_is_inside();
    failures += test_floor_one_forces_min_step();
    failures += test_proportional_mid();
    failures += test_max_step_clamp();
    failures += test_error_sign_right_down_positive();
    failures += test_negative_error_negative_delta();
    failures += test_sign_inverts_direction();
    failures += test_travel_clamp_upper();
    failures += test_travel_clamp_lower();
    failures += test_travel_within_unclamped();
    failures += test_travel_limit_zero_disables_clamp();
    failures += test_axes_independent();
    failures += test_subpixel_center_not_truncated();
    failures += test_null_out_safe();

    if (failures == 0) {
        printf("All vision_aim tests passed.\n");
    }
    return failures;
}
