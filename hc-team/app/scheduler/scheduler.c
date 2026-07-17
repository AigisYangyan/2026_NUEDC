/**
 * @file    scheduler.c
 * @brief   运行条目调度器实现——状态全私有（V13 替代前提），零下层依赖。
 */
#include "app/scheduler/scheduler.h"

#include <stddef.h>

static const Scheduler_Entry_T *s_entries = NULL;
static uint8_t s_entry_count = 0u;
static void (*s_background_step)(uint32_t now_ms) = NULL;
static int16_t s_active = SCHEDULER_ENTRY_NONE;

void Scheduler_Init(const Scheduler_Entry_T *entries, uint8_t entry_count,
                    void (*background_step)(uint32_t now_ms))
{
    s_entries = entries;
    s_entry_count = (entries != NULL) ? entry_count : 0u;
    s_background_step = background_step;
    s_active = SCHEDULER_ENTRY_NONE;
}

uint8_t Scheduler_GetEntryCount(void)
{
    return s_entry_count;
}

const char *Scheduler_GetEntryName(uint8_t index)
{
    if (index >= s_entry_count) {
        return NULL;
    }
    return s_entries[index].name;
}

bool Scheduler_EnterEntry(uint8_t index)
{
    if (index >= s_entry_count) {
        return false;
    }
    Scheduler_LeaveEntry(); /* 转移序唯一实现点：先退旧（含同索引重启） */
    s_active = (int16_t)index;
    if (s_entries[index].on_enter != NULL) {
        s_entries[index].on_enter();
    }
    return true;
}

void Scheduler_LeaveEntry(void)
{
    int16_t leaving = s_active;

    if (leaving == SCHEDULER_ENTRY_NONE) {
        return;
    }
    /* 先清活动再调 on_exit：钩子内再调 Leave 时安全退化为 no-op */
    s_active = SCHEDULER_ENTRY_NONE;
    if (s_entries[leaving].on_exit != NULL) {
        s_entries[leaving].on_exit();
    }
}

int16_t Scheduler_GetActiveEntry(void)
{
    return s_active;
}

void Scheduler_Run(uint32_t now_ms)
{
    if (s_background_step != NULL) {
        s_background_step(now_ms);
    }
    /* 背景钩子返回后再解析活动条目：背景内 Enter 的首拍 step 同拍生效 */
    if (s_active != SCHEDULER_ENTRY_NONE) {
        const Scheduler_Entry_T *entry = &s_entries[s_active];

        if (entry->on_step != NULL) {
            entry->on_step(now_ms);
        }
    }
}
