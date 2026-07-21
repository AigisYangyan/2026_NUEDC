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
 * - GrayTest   = 12 路灰度标定助手（gray_check：注册 VOFA tx×12 + 10ms 读发 0/1、OLED 四行
 *   标定面板（实时/粘滞/跳变/十六进制）、退页清表——不驱动电机；阈值在硬件电位器，固件只做校验）。
 * - LineFollow = 循迹外环运行（line_follow：进页 Init 归零→ParamTune_Init 重推持久增益→Start，
 *                10ms 泵 Update 级联 Chassis 沿线跑，退页 LineFollow_Stop 安全停车；只跑不接遥测）。
 * - ProfiledStraight = 定长梯形剖面直行（motion：进页 Init→ParamTune_Init 重推持久剖面参数+距离→
 *                StartProfiledStraight(dist,false)，10ms 泵 Update 级联 Chassis 跑一段停，退页 Motion_Stop）。
 * - GimbalTune = 云台位置环静态调参（W8 §30，VOFA 命令模式：进页 Gimbal_Init→进 GIMBAL_AIM
 *                profile（安全 cmd DB=10000 零出力）→发占位选题握手；10ms 泵 Tuning_Update
 *                （XP/XD/YP/YD/DB/MS/GO 命令 + 13 通道波形），退页 Tuning_ExitProfile 安全停；
 *                调好终值回填 s_gt_cfg 编译期默认，不入 flash）。
 * RUN_ACTIVE 期 OLED 统一 RUNNING 横幅由 menu 框架负责；例外：GrayTest 经
 * Menu_SetEntrySelfDraw 登记 self-draw（W7 §29 opt-in），活动期整屏归 gray_check 面板。
 * 换/加 debug/test 项：在 s_entries[] 补条目 + 在对应分组的 entries 数组补其下标。
 *
 * 参数组（与 DEBUG 平级的 MENU_GROUP_PARAM，进入二级界面均显示 PARAMS）：
 * - TUNE  = 循迹外环差速 PID 三增益；
 * - DRIVE = motion 剖面 4 参数（cruise/start/accel/decel）+ 测试距离 Dist。
 * 每组 SAVE 动作项一次性写片内 flash（掉电保存，共享同一 blob）。值/换算/持久化归 param_tune，
 * 菜单零复做；开机 ParamTune_Init 载入持久值应用到 line_follow + motion。加调参项：在对应 s_*_params[] 补一行。
 */
#include "app/system/app_compose.h"

#include <stddef.h>
#include <stdint.h>

#include "app/scheduler/scheduler.h"
#include "app/service/encoder_test/encoder_test.h"
#include "app/service/gimbal/gimbal.h"
#include "app/service/gray_check/gray_check.h"
#include "app/service/line_follow/line_follow.h"
#include "app/service/motion/motion.h"
#include "app/service/motor_check/motor_check.h"
#include "app/service/param_tune/param_tune.h"
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
    GrayCheck_Start();      /* 注册 VOFA tx×12（G1..G12 灰度 0/1）+ 标定统计清零，不发电机命令 */
}

static void graytest_step(uint32_t now_ms)
{
    GrayCheck_Update(now_ms);   /* 10ms 自门控读 12 路 + 发帧 + 100ms OLED 标定面板（self-draw） */
}

static void graytest_exit(void)
{
    GrayCheck_Stop();       /* 退页：清 VOFA 表 */
}

/* ---- LineFollow 运行条目钩子（→ line_follow 循迹外环服务）-------------------
 * 进页起步循迹、每拍自门控推进（内含 Chassis_Update 级联）、退页确定性停车。
 * on_enter 接线序（关闭拓扑 V28）：先 LineFollow_Init 存配置并把外环增益归零，再
 * ParamTune_Init 重推 SAVE 的持久增益（或默认），否则会用零增益跑一拍；末 LineFollow_Start
 * （配置无效则安全保持 IDLE，不发底盘目标）。差速限幅/换向/超时/斜率/丢线回退全归下游
 * 既有单一所有者（PID cfg/motor.c/speed_plan/lost_line），本装配层零复做（§8.1）。 */

/* 循迹运行条目配置：保守 UNCALIBRATED 占位——上车后按实测替换（几何/速度/阈值）。
 * 外环 PID 增益不在此表（走 TUNE 组 / param_tune 持久化）。安全项取保守值：
 * 低速起步、丢线超时有界、循迹元素检测先关（element_enable_mask=0）。 */
static const LineFollow_Config_T s_lf_cfg = {
    .pitch_mm              = 12.0f,   /* UNCALIBRATED：相邻探头中心间距实测填入 */
    .bit0_is_left          = true,    /* UNCALIBRATED：按位图 bit0 实际物理端核对（错则转向发散） */
    .straight_speed_mps    = 0.25f,   /* 保守低速起步（原 base_speed 语义） */
    .min_speed_mps         = 0.12f,   /* 入弯最低基速（≤ straight） */
    .curve_error_mm        = 20.0f,   /* UNCALIBRATED：达 min_speed 的误差幅值阈 */
    .accel_mps_per_s       = 0.5f,    /* UNCALIBRATED：提速斜坡速率上限 */
    .decel_mps_per_s       = 1.0f,    /* UNCALIBRATED：降速斜坡速率上限（略快，入弯更稳） */
    .diff_limit_mps        = 0.30f,   /* 差速修正限幅（外环 PID out_limit），保守 */
    .recovery_error_mm     = 32.4f,   /* 丢线回退幅值 ≈ 2.7 × pitch_mm（头文件建议式） */
    .lost_timeout_ms       = 1500u,   /* 丢线恢复上限，超时 → LOST 停车（有界安全项） */
    .full_bar_min_count    = 8u,      /* element_enable_mask=0 时不生效，占位 */
    .branch_min_span       = 4u,      /* 同上，占位 */
    .element_confirm_ticks = 3u,      /* 同上，占位 */
    .element_enable_mask   = 0u,      /* 先不检测循迹元素（本条目只跑基本循迹） */
};

static void linefollow_enter(void)
{
    LineFollow_Init(&s_lf_cfg);   /* 存配置 + 外环增益归零 + 速度规划/元素检测复位 → IDLE，不动底盘 */
    ParamTune_Init();             /* 重推持久化(或默认)外环增益 → LineFollow_SetGains（关闭 V28） */
    (void)LineFollow_Start();     /* 配置有效(pitch>0 且 diff_limit>0) → TRACKING；否则安全保持 IDLE */
}

static void linefollow_step(uint32_t now_ms)
{
    (void)now_ms;   /* line_follow 自持 10ms Clock 门控（单一所有者），不用注入时刻 */
    LineFollow_Update();    /* 推进外环；TRACKING/RECOVERING 期末尾级联 Chassis_Update() */
}

static void linefollow_exit(void)
{
    LineFollow_Stop();      /* 退页：→IDLE + Chassis_Stop() 确定性停车 */
}

/* ---- ProfiledStraight 运行条目钩子（→ motion 定长梯形剖面直行）--------------
 * 进页跑一段定长直行（距离取 DRIVE 组按钮值，默认 1000mm）、退页确定性停车。
 * on_enter 接线序（关闭 V28）：Motion_Init 重置(归零剖面参数为 s_ms_cfg 值) → ParamTune_Init
 * 重推持久(或默认)剖面参数到 motion + 载入测试距离 → StartProfiledStraight（dist<=0 则安全保持
 * IDLE 不动底盘）。纵向剖面/限幅归 move_profile，换向/超时/刹车归 motor.c，本装配层零复做（§8.1）。
 * heading_hold=false：开环直行测纵向剖面（不依赖 IMU/航向 PID 标定）。 */

/* 定长直行运行条目配置：保守 UNCALIBRATED 占位——上车按实测替换（尤其 mm_per_pulse）。
 * 剖面 4 参数(profile_*)进页会被 ParamTune_Init 重推覆盖，此处填与 param_tune 默认一致的占位值；
 * 未用字段(turn/arc/hold*)取安全占位（heading_hold=false 时航向 PID 不生效）。 */
static const Motion_Config_T s_ms_cfg = {
    .mm_per_pulse        = 0.1f,    /* UNCALIBRATED：脉冲→mm 实测标定（错则距离全错） */
    .heading_sign        = 1.0f,    /* 占位（heading_hold=false 不生效） */
    .straight_speed_mps  = 0.20f,   /* 恒速直行基速（本条目走剖面，不用此字段） */
    .profile_cruise_mps  = 0.20f,   /* 进页被 ParamTune_Init 覆盖；占位=param_tune 默认 */
    .profile_start_mps   = 0.08f,
    .profile_accel_mps2  = 0.30f,
    .profile_decel_mps2  = 0.30f,
    .turn_speed_mps      = 0.20f,   /* 未用（不调 Turn），安全占位 */
    .arc_speed_mps       = 0.20f,   /* 未用（不调 Arc），安全占位 */
    .track_width_mm      = 100.0f,  /* 未用（不调 Arc），>0 占位 */
    .hold_kp             = 0.0f,    /* heading_hold=false → 航向 PID 不生效 */
    .hold_ki             = 0.0f,
    .hold_kd             = 0.0f,
    .hold_diff_limit_mps = 0.15f,   /* PID out_limit 占位（不生效） */
    .turn_kp             = 0.0f,    /* 未用 */
    .straight_tol_mm     = 5.0f,    /* 到位容差（mm） */
    .turn_tol_deg        = 2.0f,    /* 未用 */
    .profile_timeout_ticks = 1500u, /* §8.1 防过冲：~15s(10ms/拍)运行上限（移动但永不达标兜底） */
    .profile_stall_ticks   = 80u,   /* §8.1 防堵转：~0.8s 命令在动但无进展即切停，保护 TB6612（起步卡住/脱线） */
};

static void profiledstraight_enter(void)
{
    Motion_Init(&s_ms_cfg);   /* 重置服务 + odometry/航向 PID Init → IDLE，不发电机命令 */
    ParamTune_Init();         /* 重推持久(或默认)剖面参数 → Motion_SetProfileParams + 载入距离（关闭 V28） */
    (void)Motion_StartProfiledStraight(ParamTune_GetDist_mm(), false);  /* dist<=0 → 安全保持 IDLE */
}

static void profiledstraight_step(uint32_t now_ms)
{
    (void)now_ms;   /* motion 自持节奏：Imu 排空 + 里程计消费 + 剖面律 + 末尾 Chassis_Update 自门控 */
    Motion_Update();
}

static void profiledstraight_exit(void)
{
    Motion_Stop();  /* 退页：Chassis_Stop 确定性停车 → IDLE */
}

/* ---- GimbalTune 运行条目钩子（→ tuning GIMBAL_AIM profile，W8 §30）---------
 * 进页零出力（安全 cmd DB=10000）、VOFA 命令改 PD 参数看波形、退页确定性安全停。
 * 顺序约束（契约 §30）：SelectTopic 必须在 EnterProfile 之后——Enter 内 Gimbal_Stop
 * 会终止进行中的握手。握手/坐标超时 → STOPPED 后，上位机发 GO=1 重握手（不丢调参会话）。
 * PD 数学/死区/floor/步长/极性/轴程归 vision_aim，RPM 限幅归 emm42，清洗归 tuning_gimbal，
 * 本装配层零复做（§8.1/§8.2）。 */

/* 调参会话占位选题号：按视觉组约定的「静态目标持续出坐标」任务号填写（现场可改此处）。 */
#define GIMBALTUNE_TOPIC_MAIN 1u
#define GIMBALTUNE_TOPIC_SUB  0u

/* 云台调参条目配置：UNCALIBRATED 占位——上机按实物替换（尤其 center/sign/travel_limit）。
 * aim 四调参字段（kp/kd/deadband/max_step）进页即被 EnterProfile 安全 cmd 覆写，此处填
 * 同款安全值防「Init 到 Enter 之间」的空窗；调参定型后把 VOFA 终值回填到这四组字段。 */
static const Gimbal_Config_T s_gt_cfg = {
    .aim = {
        .center_px          = { 320.0f, 240.0f },     /* UNCALIBRATED：视觉分辨率中心（x,y） */
        .deadband_px        = { 10000.0f, 10000.0f }, /* 安全占位；终值=噪声测定后回填 */
        .kp                 = { 0.0f, 0.0f },         /* 终值=VOFA 调参定型后回填 */
        .kd                 = { 0.0f, 0.0f },
        .max_step_pulse     = { 1, 1 },
        .sign               = { 1, 1 },               /* UNCALIBRATED：极性首验 SOP 后定（docs） */
        .travel_limit_pulse = { 800, 400 },           /* UNCALIBRATED：机械行程软限位（脉冲） */
    },
    .step_speed_rpm  = 30u,     /* 保守起步；emm42 限幅唯一所有者夹 ≤100 */
    .coord_timeout_ms = 500u,   /* 坐标停顿安全停（调参场景放宽，容忍上位机短暂丢目标） */
    .ack_timeout_ms  = 1000u,   /* 握手超时（视觉未就绪 → STOPPED，GO 重试） */
};

static void gimbaltune_enter(void)
{
    Gimbal_Init(&s_gt_cfg);       /* 配置 + 安全起点 IDLE（不发选题/不发移动/不 enable） */
    (void)Tuning_EnterProfile(TUNING_PROFILE_GIMBAL_AIM);  /* 清表/排空 RX/安全 cmd/注册/停+应用 */
    (void)Gimbal_SelectTopic(GIMBALTUNE_TOPIC_MAIN, GIMBALTUNE_TOPIC_SUB); /* 失败→GO 重试 */
}

static void gimbaltune_step(uint32_t now_ms)
{
    (void)now_ms;   /* tuning/gimbal 各自 Clock 门控 10ms（单一所有者），不用注入时刻 */
    Tuning_Update();
}

static void gimbaltune_exit(void)
{
    Tuning_ExitProfile();   /* 退页：Gimbal_Stop（保持力矩安全态）+ 清 VOFA 表 */
}

/* ---- 运行条目表（scheduler 全局条目索引 = 本数组下标）----------------------- */

/* GrayTest 的条目下标（self-draw 登记与 s_entries[] 同源对齐；插入条目时同步改此值）。 */
#define APP_ENTRY_IDX_GRAYTEST 3u

static const Scheduler_Entry_T s_entries[] = {
    { "SpeedTune",   speedtune_enter, speedtune_step, speedtune_exit },  /* idx 0 */
    { "EncoderTest", enctest_enter,   enctest_step,   enctest_exit },    /* idx 1 */
    { "MotorDir",    motordir_enter,  motordir_step,  motordir_exit },   /* idx 2 */
    { "GrayTest",    graytest_enter,  graytest_step,  graytest_exit },   /* idx 3 = APP_ENTRY_IDX_GRAYTEST */
    { "LineFollow",  linefollow_enter, linefollow_step, linefollow_exit }, /* idx 4 */
    { "ProfiledStraight", profiledstraight_enter, profiledstraight_step, profiledstraight_exit }, /* idx 5 */
    { "GimbalTune",  gimbaltune_enter, gimbaltune_step, gimbaltune_exit }, /* idx 6 */
};

/* ---- 菜单分组表（DEBUG 运行分类的条目 = 上表下标）--------------------------- */

static const uint8_t s_debug_entries[] = { 0u, 1u, 2u, 3u, 4u, 5u, 6u };  /* → SpeedTune / EncoderTest / MotorDir / GrayTest / LineFollow / ProfiledStraight / GimbalTune */

/* TUNE 参数组：循迹外环差速 PID 三增益（milli 口径）+ SAVE 动作项。
 * get/set 委派 param_tune（值/换算/持久化归它）；SAVE 的 action=ParamTune_Save（K3 即存 flash）。
 * step 为 param_tune 导出的占位常量（现场再定）。菜单零换算/零限幅/零值副本。 */
static const Menu_Param_T s_tune_params[] = {
    { "LF Kp", ParamTune_GetKp_milli, ParamTune_SetKp_milli, TUNE_STEP_KP_MILLI, NULL },
    { "LF Ki", ParamTune_GetKi_milli, ParamTune_SetKi_milli, TUNE_STEP_KI_MILLI, NULL },
    { "LF Kd", ParamTune_GetKd_milli, ParamTune_SetKd_milli, TUNE_STEP_KD_MILLI, NULL },
    { "SAVE",  NULL,                  NULL,                  0,                  ParamTune_Save },
};

/* DRIVE 参数组：motion 剖面 4 参数（milli 口径）+ 测试距离（mm）+ SAVE 动作项。
 * get/set 委派 param_tune（剖面→motion、距离→param_tune 自持；换算/持久化归 param_tune）；
 * 任一 SAVE 都序列化同一 flash blob（LF 增益 + 剖面 + 距离全量）。菜单零换算/零限幅/零值副本。 */
static const Menu_Param_T s_drive_params[] = {
    { "Dist mm", ParamTune_GetDist_mm,     ParamTune_SetDist_mm,     TUNE_STEP_DIST_MM,      NULL },
    { "Cruise",  ParamTune_GetCruise_milli, ParamTune_SetCruise_milli, TUNE_STEP_CRUISE_MILLI, NULL },
    { "Start",   ParamTune_GetStart_milli,  ParamTune_SetStart_milli,  TUNE_STEP_START_MILLI,  NULL },
    { "Accel",   ParamTune_GetAccel_milli,  ParamTune_SetAccel_milli,  TUNE_STEP_ACCEL_MILLI,  NULL },
    { "Decel",   ParamTune_GetDecel_milli,  ParamTune_SetDecel_milli,  TUNE_STEP_DECEL_MILLI,  NULL },
    { "SAVE",    NULL,                      NULL,                      0,                      ParamTune_Save },
};

static const Menu_Group_T s_groups[] = {
    { "DEBUG", MENU_GROUP_RUN, s_debug_entries,
      (uint8_t)(sizeof(s_debug_entries) / sizeof(s_debug_entries[0])),
      NULL, 0u },
    { "TUNE", MENU_GROUP_PARAM, NULL, 0u,
      s_tune_params,
      (uint8_t)(sizeof(s_tune_params) / sizeof(s_tune_params[0])) },
    { "DRIVE", MENU_GROUP_PARAM, NULL, 0u,
      s_drive_params,
      (uint8_t)(sizeof(s_drive_params) / sizeof(s_drive_params[0])) },
};

/* ---- 装配入口 ----------------------------------------------------------- */

void AppCompose_Install(void)
{
    Scheduler_Init(s_entries,
                   (uint8_t)(sizeof(s_entries) / sizeof(s_entries[0])),
                   Menu_Tick);
    Menu_Setup(s_groups,
               (uint8_t)(sizeof(s_groups) / sizeof(s_groups[0])));
    /* GrayTest 自绘整屏：标定面板取代 RUNNING 横幅（W7 §29 opt-in）。 */
    Menu_SetEntrySelfDraw(APP_ENTRY_IDX_GRAYTEST);
    /* 开机把持久化循迹增益（或默认）载入并应用到 line_follow 外环 PID。
     * LineFollow 运行条目 on_enter 亦按此序（LineFollow_Init 归零后重调 ParamTune_Init 重推）。 */
    ParamTune_Init();
}
