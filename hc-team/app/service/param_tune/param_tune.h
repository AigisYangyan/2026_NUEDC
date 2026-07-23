/**
 * @file    param_tune.h
 * @brief   按钮动态调参持久化服务（App Service 层）——板载按钮调参值的 flash 持久化编排。
 *
 * 抽象（调参链路能做什么）：
 * - 开机把持久化值（或默认值）载入并应用：循迹外环 PID 增益 + motion 剖面参数 + 测试距离；
 * - 就地读/改这些值（int32 口径，即时生效，供板载按钮菜单调用）；
 * - 一次性把当前全部值存入片内 flash（掉电保存）。
 *
 * 隐藏：用了哪些 Driver/Service、blob 序列化布局（schema_ver 3）、默认/步长常量、int32↔float 定点换算细节。
 *
 * 分层与所有权（AGENTS.md §4/§8.2）：
 * - **已应用增益唯一属 line_follow**（Set/GetGains）、**已应用剖面参数唯一属 motion**
 *   （Set/GetProfileParams）；本服务对二者不持副本（Model A 无副本胶水）：get/set 直通拥有者、
 *   save→读回序列化存盘、init→读盘/默认→应用。
 * - **测试距离由本服务自持** `s_dist_mm`（唯一持值项——测试设定量无 Service 家）。
 * - **本服务唯一拥有**：持久化编排（单扇区单 blob，param_store 只存一段）+ int32 milli↔float ×1000
 *   换算（唯一 scale 所有者）+ 默认值/步长占位常量。菜单（menu/menu_param）零换算、零限幅、零值副本。
 * - NV 完整性唯一属 driver/param_store（框定/CRC/擦前写）；差速限幅唯一属外环 Pid cfg、
 *   剖面限幅唯一属 move_profile；本服务只填 payload 字段，不碰 Driver 框定。
 * - 与 VOFA 静态调参（tuning/S03）是两条平行链，互不联通。
 * - blob schema_ver 1→2：旧 13B 记录因 ParamStore_Read(len=33) 长度不符被忽略→全默认（一次性丢旧 LF 增益）。
 *
 * 调用前置条件：System 装配层已完成 param_store 所需的 flash 控制器上电；
 * ParamTune_Init 应在开机装配时调用一次。**接线注意**：未来若有先 LineFollow_Init（会归零增益）
 * 再运行循迹的运行条目，其 on_enter 须在 LineFollow_Init 之后重调 ParamTune_Init 重推持久增益。
 *
 * 单位约定：增益以 int32 **milli 口径**读写（显示值 1200 = 实际 1.200）。步长每次加减单位
 * 为占位常量 TUNE_STEP_*_MILLI（现场再定）。
 */
#ifndef HC_TEAM_APP_SERVICE_PARAM_TUNE_PARAM_TUNE_H
#define HC_TEAM_APP_SERVICE_PARAM_TUNE_PARAM_TUNE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 每次 UP/DOWN 的调整步长（milli 口径；按键无连发=一步一按，按「量程/步长≈10~25 下」
 * 定，§37.7 步长审计）。LF 增益量程 5~50 milli → 步长 2（原 10 会跳过半数可用值）。 */
#define TUNE_STEP_KP_MILLI 2
#define TUNE_STEP_KI_MILLI 2
#define TUNE_STEP_KD_MILLI 2
/* DRIVE 组：motion 剖面参数（milli 口径 = 实值 ×1000）+ 测试距离（mm 直读）。 */
#define TUNE_STEP_CRUISE_MILLI 20
#define TUNE_STEP_START_MILLI  10
#define TUNE_STEP_ACCEL_MILLI  50
#define TUNE_STEP_DECEL_MILLI  50
#define TUNE_STEP_DIST_MM      50
/* CHAS 组：底盘速度环增益（milli，双轮同值应用）。量纲=PWM/(m/s)，真实值百位量级
 * （milli 屏显 ≈ 1e5），步长按此定 10.0/次——原占位 0.01/次按不动（量级账修正）。 */
#define TUNE_STEP_CKP_MILLI 10000
#define TUNE_STEP_CKI_MILLI 10000
#define TUNE_STEP_CKD_MILLI 1000
/* HEAD 组：航向调参（H 增益与 LF 同量级 → 步长 2）+ 转弯测试角（负值=反向转，合法）。 */
#define TUNE_STEP_HKP_MILLI 2
#define TUNE_STEP_HKI_MILLI 2
#define TUNE_STEP_HKD_MILLI 2
#define TUNE_STEP_HTKP_MILLI 5
#define TUNE_STEP_TURN_DEG  15

/** 开机载入：flash 有效记录(schema_ver 3)→应用 LF/底盘/航向增益+剖面参数+距离+转角；否则全默认→应用。装配时调用一次。 */
void ParamTune_Init(void);

/** 读回当前循迹外环增益（milli 口径 = LineFollow 实值 ×1000）。 */
int32_t ParamTune_GetKp_milli(void);
int32_t ParamTune_GetKi_milli(void);
int32_t ParamTune_GetKd_milli(void);

/** 设置循迹外环增益（milli 口径 /1000 → LineFollow_SetGains，即时生效；保另两增益现值）。 */
void ParamTune_SetKp_milli(int32_t v);
void ParamTune_SetKi_milli(int32_t v);
void ParamTune_SetKd_milli(int32_t v);

/** 读回当前 motion 剖面参数（milli 口径 = Motion 实值 ×1000；委派 Motion_GetProfileParams）。 */
int32_t ParamTune_GetCruise_milli(void);
int32_t ParamTune_GetStart_milli(void);
int32_t ParamTune_GetAccel_milli(void);
int32_t ParamTune_GetDecel_milli(void);

/** 设置 motion 剖面参数（milli /1000 → Motion_SetProfileParams，即时生效；保另三参数现值）。 */
void ParamTune_SetCruise_milli(int32_t v);
void ParamTune_SetStart_milli(int32_t v);
void ParamTune_SetAccel_milli(int32_t v);
void ParamTune_SetDecel_milli(int32_t v);

/** 测试运行距离（mm 直读）。本值由 param_tune 自持（唯一持值项——测试设定量无 Service 所有者）。 */
int32_t ParamTune_GetDist_mm(void);
void    ParamTune_SetDist_mm(int32_t v);

/* ---- PT3v §37：底盘速度环增益（委派 chassis，双轮同值应用；读回取左轮）------ */
int32_t ParamTune_GetCKp_milli(void);
int32_t ParamTune_GetCKi_milli(void);
int32_t ParamTune_GetCKd_milli(void);
void    ParamTune_SetCKp_milli(int32_t v);
void    ParamTune_SetCKi_milli(int32_t v);
void    ParamTune_SetCKd_milli(int32_t v);

/* ---- PT3v §37：motion 航向调参（委派 motion；turn_kp/hold 均即时生效——§37.5 双写）*/
int32_t ParamTune_GetHKp_milli(void);
int32_t ParamTune_GetHKi_milli(void);
int32_t ParamTune_GetHKd_milli(void);
int32_t ParamTune_GetHTKp_milli(void);
void    ParamTune_SetHKp_milli(int32_t v);
void    ParamTune_SetHKi_milli(int32_t v);
void    ParamTune_SetHKd_milli(int32_t v);
void    ParamTune_SetHTKp_milli(int32_t v);

/** 转弯测试角（度直读，默认 90）。本服务自持（测试设定量，dist_mm 先例）。 */
int32_t ParamTune_GetTurnDeg(void);
void    ParamTune_SetTurnDeg(int32_t v);

/** 把当前全部调参值序列化(schema_ver 3)存入片内 flash（掉电保存）。菜单 SAVE 项 K3 触发。 */
void ParamTune_Save(void);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_APP_SERVICE_PARAM_TUNE_PARAM_TUNE_H */
