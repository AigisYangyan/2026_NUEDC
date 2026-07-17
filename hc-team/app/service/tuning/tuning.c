/**
 * @file    tuning.c
 * @brief   VOFA 调参链路服务：profile 生命周期 + 10ms 自门控推进编排。
 *
 * 编排职责边界：
 * - 本文件只碰 VOFA 驱动（clear/run）与 profile 子模块调度；
 *   对被调 Service 的一切访问都在各 profile 子模块内（tuning_chassis.c）。
 * - vofa_run() 内完成 RX 字节流解析（V09 任务上下文边界）与上一拍 tx 帧发送；
 *   本服务负责把解析结果（cmd 组）应用出去——分发/应用唯一收口（契约 §9 Q2 定案）。
 */
#include "app/service/tuning/tuning.h"

#include "app/service/tuning/tuning_chassis.h"
#include "driver/clock/clock.h"
#include "driver/uart_vofa/uart_vofa.h"

#define TUNING_STREAM_PERIOD_MS 10u

static Tuning_Profile s_active = TUNING_PROFILE_NONE;
static uint32_t s_period_base_ms;

void Tuning_Init(void)
{
    s_active = TUNING_PROFILE_NONE;
    s_period_base_ms = 0u;
}

bool Tuning_EnterProfile(Tuning_Profile profile)
{
    if (profile != TUNING_PROFILE_CHASSIS_SPEED) {
        Tuning_ExitProfile();   /* NONE 态下是无副作用空操作 */
        return false;
    }

    vofa_clear_profile();
    vofa_run();     /* 排空 NONE 期间积压的 RX：绑定/通道表已清空，解析落空且无帧发出。
                     * 缺此步，会话间上位机残留命令会在重进后首拍生效，破坏安全初值（契约修订 1）。 */
    TuningChassis_Enter();
    s_active = TUNING_PROFILE_CHASSIS_SPEED;
    s_period_base_ms = Clock_NowMs();
    return true;
}

void Tuning_Update(void)
{
    uint32_t now_ms;

    if (s_active != TUNING_PROFILE_CHASSIS_SPEED) {
        return;
    }

    now_ms = Clock_NowMs();
    if ((now_ms - s_period_base_ms) >= TUNING_STREAM_PERIOD_MS) { /* 无符号减法天然处理回绕 */
        s_period_base_ms = now_ms;
        vofa_run();                 /* RX 解析→cmd + 发送上一拍 tx 帧 */
        TuningChassis_Apply();      /* cmd → Chassis 公共 API，单向 */
        TuningChassis_RefreshTx();  /* 快照 → tx，下一拍发出（晚一帧，接受） */
    }

    TuningChassis_PumpInner();      /* 恒推进内环（内环自门控 10ms） */
}

void Tuning_ExitProfile(void)
{
    if (s_active == TUNING_PROFILE_NONE) {
        return;
    }

    TuningChassis_Exit();
    vofa_clear_profile();
    s_active = TUNING_PROFILE_NONE;
}

Tuning_Profile Tuning_GetActiveProfile(void)
{
    return s_active;
}
