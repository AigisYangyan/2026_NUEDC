/**
 * @file    tuning.h
 * @brief   VOFA 调参链路服务（App Service 层）——调参 profile 生命周期与推进的唯一所有者。
 *
 * 抽象（调参链路能做什么）：
 * - 进入/退出一个调参 profile（进入即注册 VOFA 变量组并置安全初值）；
 * - 周期推进：接收上位机命令 → 经被调 Service 公共 API 单向应用 → 刷新遥测 → 发帧；
 * - 报告当前激活 profile。
 *
 * 隐藏：
 * - VOFA 驱动注册细节、变量组存储、应用节奏、协议与串口。
 *
 * 变量组隔离三原则（用户裁定 2026-07-17，契约 §9）：
 * 1. VOFA 变量组只做调参，实际运行变量不注册进 VOFA；
 * 2. cmd 组与运行变量分离、不相互赋予——参数应用方向唯一：cmd → Service 公共 API，
 *    遥测 tx 组只是 GetTelemetry 快照的单向副本；
 * 3. Enter/重进一律重置 cmd 组为安全值（增益 0、目标 0），悬挂调参上电确定性不出力。
 *
 * 分层与所有权：
 * - 字节流解析归 Driver vofa_run()（V09 任务上下文边界）；分发与应用归本服务唯一收口。
 * - 增益/目标写入只经 Chassis 公共 API；限幅/slew/换向/超时/刹车各归既有所有者，零复做。
 *
 * 调用前置条件：
 * - System 装配层已完成 vofa_init()（含 VofaUart_Init）与 Clock/底盘链 Init。
 */
#ifndef TUNING_H
#define TUNING_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 调参 profile：一组 VOFA 变量 + 一条应用链路。 */
typedef enum {
    TUNING_PROFILE_NONE = 0,
    TUNING_PROFILE_CHASSIS_SPEED,   /* 底盘速度环脱线悬挂调参 */
} Tuning_Profile;

/** 初始化/重置服务：回到 NONE。静默：不碰 VOFA 驱动、不碰底盘。 */
void Tuning_Init(void);

/**
 * @brief  进入调参 profile：清空 VOFA profile → cmd 组重置安全值 → 注册变量组
 *         → 被调服务确定性停止 → 立即应用安全 cmd（覆写底盘残留增益/目标）。
 * @param  profile  目标 profile；NONE 或未知值不进入任何 profile（当前激活时等效 Exit）。
 * @return true = 已进入；false = 未进入（NONE/未知值）。
 */
bool Tuning_EnterProfile(Tuning_Profile profile);

/**
 * @brief  推进调参链路。NONE 态完全静默（不发帧、不推底盘）。激活态每次调用：
 *         按 10ms 自门控执行 vofa_run（Driver 内解析 RX + 发送上一拍遥测帧）
 *         → cmd 无条件应用 → 刷新遥测快照（比现场晚一帧）；
 *         无论到期与否，末尾恒推进 Chassis_Update（内环自门控）。
 */
void Tuning_Update(void);

/**
 * @brief  退出调参：被调服务确定性停止 + 清空 VOFA profile → NONE。
 * @note   此后 Update 静默不再推进内环，刹车真值表保持（驻车语义同 chassis.h 文档）。
 *         NONE 态调用为无副作用空操作。
 */
void Tuning_ExitProfile(void);

/** 当前激活 profile。 */
Tuning_Profile Tuning_GetActiveProfile(void);

#ifdef __cplusplus
}
#endif

#endif /* TUNING_H */
