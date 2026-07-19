/**
 * @file    param_tune.h
 * @brief   按钮动态调参持久化服务（App Service 层）——循迹外环增益的 flash 持久化编排。
 *
 * 抽象（调参链路能做什么）：
 * - 开机把持久化增益（或默认值）载入并应用到循迹外环；
 * - 就地读/改一组循迹外环 PID 增益（int32 milli 口径，即时生效，供板载按钮菜单调用）；
 * - 一次性把当前增益存入片内 flash（掉电保存）。
 *
 * 隐藏：用了哪些 Driver/Service、blob 序列化布局、默认/步长常量、int32↔float 定点换算细节。
 *
 * 分层与所有权（AGENTS.md §4/§8.2）：
 * - **已应用增益唯一属 line_follow**（Set/GetGains）；本服务不持增益副本（Model A 无副本胶水）：
 *   get→LineFollow_GetGains、set→LineFollow_SetGains（即时生效）、save→读回增益序列化存盘、
 *   init→读盘/默认→SetGains 应用。
 * - **本服务唯一拥有**：持久化编排 + int32 milli↔float ×1000 换算（唯一 scale 所有者）+
 *   默认增益/步长占位常量。菜单（menu/menu_param）零换算、零限幅、零值副本。
 * - NV 完整性唯一属 driver/param_store（框定/CRC/擦前写）；差速限幅唯一属外环 Pid cfg。
 * - 与 VOFA 静态调参（tuning/S03）是两条平行链，互不联通。
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

/* 每次 UP/DOWN 的调整步长（milli 口径占位常量，现场按实际效果再定）。 */
#define TUNE_STEP_KP_MILLI 10
#define TUNE_STEP_KI_MILLI 10
#define TUNE_STEP_KD_MILLI 10

/** 开机载入：flash 有效记录→应用；否则默认增益→应用。装配时调用一次。 */
void ParamTune_Init(void);

/** 读回当前循迹外环增益（milli 口径 = LineFollow 实值 ×1000）。 */
int32_t ParamTune_GetKp_milli(void);
int32_t ParamTune_GetKi_milli(void);
int32_t ParamTune_GetKd_milli(void);

/** 设置循迹外环增益（milli 口径 /1000 → LineFollow_SetGains，即时生效；保另两增益现值）。 */
void ParamTune_SetKp_milli(int32_t v);
void ParamTune_SetKi_milli(int32_t v);
void ParamTune_SetKd_milli(int32_t v);

/** 把当前增益序列化存入片内 flash（掉电保存）。菜单 SAVE 项 K3 触发。 */
void ParamTune_Save(void);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_APP_SERVICE_PARAM_TUNE_PARAM_TUNE_H */
