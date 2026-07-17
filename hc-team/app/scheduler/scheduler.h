/**
 * @file    scheduler.h
 * @brief   运行条目调度器（App Scheduler 层）——条目生命周期与每拍泵送的唯一机制点。
 *
 * 抽象（调度器能做什么）：
 * - 登记一组命名运行条目（Task/UI 同层在装配时提供）；
 * - 进入/离开/重启条目，查询当前条目与名称（菜单渲染所需）；
 * - 被喂时驱动一拍：背景钩子先行，随后活动条目 step。
 *
 * 隐藏：
 * - 条目表指针、活动索引、背景钩子——全部私有，本头文件零 extern 变量。
 *
 * 时间来源（Q1 定案，2026-07-17）：
 * - 本模块不含 clock.h；now_ms 由 System 装配层读 Clock_NowMs() 后经
 *   Scheduler_Run(now_ms) 参数注入，并原值透传给全部钩子——Task/UI 层
 *   依赖矩阵禁调 Driver，此参数是它们唯一合法时间来源。
 *
 * 分层与所有权：
 * - run-entry 转移序（先 on_exit 旧、再 on_enter 新）唯一实现点在
 *   Scheduler_EnterEntry；钩子提供者不得自行补第二份转移逻辑。
 * - 单活动条目不变量：任意时刻至多一个条目在被 step——这是 line_follow 与
 *   tuning 各自恒推 Chassis_Update() 的双泵风险的结构性排除手段。
 * - 时间片/周期门控不在本层：各 Service 自门控，条目 step 每拍无条件调用。
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Scheduler_GetActiveEntry() 的「无活动条目」返回值（旧 IDLE 语义）。 */
#define SCHEDULER_ENTRY_NONE (-1)

/** 运行条目：名称 + 三个生命周期钩子（钩子允许 NULL = 跳过）。 */
typedef struct {
    const char *name;                  /* ASCII 条目名，菜单渲染用；不得为 NULL */
    void (*on_enter)(void);            /* 进入时一次 */
    void (*on_step)(uint32_t now_ms);  /* 活动期每拍无条件调用 */
    void (*on_exit)(void);             /* 离开/切换/重启时一次 */
} Scheduler_Entry_T;

/**
 * @brief  登记条目表与可选背景钩子，活动条目复位为无。
 * @param  entries          条目表；调用方保证其生命周期覆盖使用期。
 *                          NULL（配合 entry_count=0）为合法空表。
 * @param  entry_count      条目数；0 时 Enter 恒 false。
 * @param  background_step  每拍先行的背景钩子（UI/菜单泵送位），NULL 允许。
 * @note   装配级复位：不触碰硬件/Service，不触发任何 on_exit——运行级转移
 *         语义只存在于 Enter/Leave。
 */
void Scheduler_Init(const Scheduler_Entry_T *entries, uint8_t entry_count,
                    void (*background_step)(uint32_t now_ms));

/** 已登记条目数。 */
uint8_t Scheduler_GetEntryCount(void);

/** 条目名；index 越界返回 NULL。 */
const char *Scheduler_GetEntryName(uint8_t index);

/**
 * @brief  进入条目：先 on_exit(旧活动条目，若有)，再置活动，再 on_enter(新)。
 *         同索引重进 = 重启（同样 exit→enter 序）。
 * @return true = 已进入；false = index 越界或空表（零副作用）。
 */
bool Scheduler_EnterEntry(uint8_t index);

/** 离开活动条目（on_exit + 清活动）；无活动条目时 no-op。 */
void Scheduler_LeaveEntry(void);

/** 活动条目索引；无活动条目返回 SCHEDULER_ENTRY_NONE。 */
int16_t Scheduler_GetActiveEntry(void);

/**
 * @brief 驱动一拍：① background_step(now_ms)（非 NULL 时无条件先行）；
 *        ② 随后解析活动条目并调其 on_step(now_ms)——背景钩子内 EnterEntry
 *        的首拍 step 同拍生效。钩子内允许调 Enter/Leave，立即生效：
 *        on_step 内 LeaveEntry → 本条目 on_exit 即刻执行，本拍不再有条目 step。
 * @param now_ms 毫秒时刻（System 装配层供给），原值透传给钩子。
 */
void Scheduler_Run(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SCHEDULER_H */
