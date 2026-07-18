/**
 * @file    fake_bsl_invoke.c
 * @brief   BslEntry_InvokeBsl 的主机替身 —— 计数代替真实 asm(擦 SRAM+复位进 BSL)
 *
 * 真实实现（bsl_entry_invoke.c）是内联汇编 + SYSCTL 复位，永不返回、无法在主机 x86 验证。
 * 主机测试只验证「判定逻辑」：命中 0x22 是否恰好调一次 InvokeBsl、非触发字节是否零调用。
 * 真实的擦除+跳转由用户上板验证（硬件行不入验收，契约 §6）。
 */
#include "driver/bsl_entry/bsl_entry.h"

static unsigned int s_invoke_count = 0u;

void BslEntry_InvokeBsl(void)
{
    /* 真实实现永不返回；替身计数后返回，便于测试继续断言。 */
    s_invoke_count++;
}

unsigned int FakeBslInvoke_Count(void)
{
    return s_invoke_count;
}

void FakeBslInvoke_Reset(void)
{
    s_invoke_count = 0u;
}
