/**
 * @file    app_compose.c
 * @brief   World-2 运行栈装配：运行条目表 + 菜单分组表 + 生命周期钩子适配。
 *
 * 交互模型（计划表 §22.0.3）：
 * - 空转态：scheduler 每拍只泵背景钩子 Menu_Tick（HMI：OLED 刷新 + 按键中断事件，
 *   无事件不重渲染），别的后台全不开；
 * - 按键在菜单选中某运行条目 → Scheduler_EnterEntry → on_enter 注册作用域数据组、
 *   on_step 每拍泵作用域服务、BACK → on_exit 停；
 * - 周期归各 Service 自门控（单一所有者：速度环 10ms = Chassis 内部 Clock 门控）。
 *
 * DEBUG 组运行条目（三钩子派发对应 Service，进页注册/驱动、退页停）：
 * - SpeedTune  = 底盘速度环 VOFA 调参（tuning：注册 VOFA 表、10ms 泵速度环 + 发帧、退页刹停清表）；
 * - EncoderTest= 编码器脉冲遥测（encoder_test：注册 VOFA tx×4、10ms 采样发帧、退页清表——不驱动电机）；
 * - MotorDir   = 电机方向自检（motor_check：两轮同向 ±200 前后 2s 循环、退页 Motor_BrakeAll）；
 * - GrayTest   = 12 路灰度数字量遥测（gray_check：注册 VOFA tx×12、10ms 读发 0/1、退页清表——不驱动电机）。
 * RUN_ACTIVE 期 OLED 统一 RUNNING 横幅由 menu 框架负责（各条目不碰 OLED）。
 * 换/加 debug/test 项：在 s_entries[] 补条目 + 在对应分组的 entries 数组补其下标。
 */
#include "app/system/app_compose.h"

#include <stddef.h>
#include <stdint.h>

#include "app/scheduler/scheduler.h"
#include "app/service/encoder_test/encoder_test.h"
#include "app/service/gray_check/gray_check.h"
#include "app/service/motor_check/motor_check.h"
#include "app/service/tuning/tuning.h"
#include "app/ui/menu/menu.h"

/* ---- SpeedTune 运行条目钩子（适配 Scheduler_Entry_T 签名 → tuning 服务）------ */

static void speedtune_enter(void)
{
    /* 进页即注册 VOFA 速度环调参组 + 确定性安全停（tuning 内部清表/排空/置安全 cmd）。 */
    (void)Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED);
}

static void speedtune_step(uint32_t now_ms)
{
    (void)now_ms;   /* tuning 是 Service，周期由其自身 Clock 门控（10ms 单一所有者），不用注入时刻 */
    Tuning_Update();
}

static void speedtune_exit(void)
{
    Tuning_ExitProfile();   /* 退页：Chassis_Stop 刹停 + 清 VOFA 表 */
}

/* ---- EncoderTest 运行条目钩子（→ encoder_test 服务，now_ms 透传注入）--------- */

static void enctest_enter(void)
{
    EncoderTest_Start();    /* 注册 VOFA tx×4（脉冲/速度 L/R），不发电机命令 */
}

static void enctest_step(uint32_t now_ms)
{
    EncoderTest_Update(now_ms); /* 10ms 自门控采样 + 发帧（now_ms 由 scheduler 注入） */
}

static void enctest_exit(void)
{
    EncoderTest_Stop();     /* 退页：清 VOFA 表 */
}

/* ---- MotorDir 运行条目钩子（→ motor_check 服务，now_ms 透传注入）------------- */

static void motordir_enter(void)
{
    MotorCheck_Start();     /* 相位复位；首 step 播种后才驱动（§8.1） */
}

static void motordir_step(uint32_t now_ms)
{
    MotorCheck_Update(now_ms);  /* 两轮同向 ±200 前后 2s 循环（now_ms 由 scheduler 注入） */
}

static void motordir_exit(void)
{
    MotorCheck_Stop();      /* 退页：Motor_BrakeAll 确定性停止 */
}

/* ---- GrayTest 运行条目钩子（→ gray_check 服务，now_ms 透传注入）------------- */

static void graytest_enter(void)
{
    GrayCheck_Start();      /* 注册 VOFA tx×12（G1..G12 灰度 0/1），不发电机命令 */
}

static void graytest_step(uint32_t now_ms)
{
    GrayCheck_Update(now_ms);   /* 10ms 自门控读 12 路 + 发帧（now_ms 由 scheduler 注入） */
}

static void graytest_exit(void)
{
    GrayCheck_Stop();       /* 退页：清 VOFA 表 */
}

/* ---- 运行条目表（scheduler 全局条目索引 = 本数组下标）----------------------- */

static const Scheduler_Entry_T s_entries[] = {
    { "SpeedTune",   speedtune_enter, speedtune_step, speedtune_exit },  /* idx 0 */
    { "EncoderTest", enctest_enter,   enctest_step,   enctest_exit },    /* idx 1 */
    { "MotorDir",    motordir_enter,  motordir_step,  motordir_exit },   /* idx 2 */
    { "GrayTest",    graytest_enter,  graytest_step,  graytest_exit },   /* idx 3 */
};

/* ---- 菜单分组表（DEBUG 运行分类的条目 = 上表下标）--------------------------- */

static const uint8_t s_debug_entries[] = { 0u, 1u, 2u, 3u };  /* → SpeedTune / EncoderTest / MotorDir / GrayTest */

static const Menu_Group_T s_groups[] = {
    { "DEBUG", MENU_GROUP_RUN, s_debug_entries,
      (uint8_t)(sizeof(s_debug_entries) / sizeof(s_debug_entries[0])),
      NULL, 0u },
};

/* ---- 装配入口 ----------------------------------------------------------- */

void AppCompose_Install(void)
{
    Scheduler_Init(s_entries,
                   (uint8_t)(sizeof(s_entries) / sizeof(s_entries[0])),
                   Menu_Tick);
    Menu_Setup(s_groups,
               (uint8_t)(sizeof(s_groups) / sizeof(s_groups[0])));
}
