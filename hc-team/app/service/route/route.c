/**
 * @file    route.c
 * @brief   分段路线执行服务实现（S07，phase4 计划表 §20）。
 *
 * 段状态机（Route_Update(now_ms) 在 RUNNING 态，seg = &s_segments[s_cur]）：
 *
 *   ┌─ s_state != RUNNING ─────────► 静默返回（IDLE/DONE/FAULT：不推任何子服务，刹车保持）
 *   │
 *   ├─ 段未进入（!s_entered）────────► 只进入、不驱动、本拍返回：
 *   │     FOLLOW_UNTIL : LineFollow_Start()          失败 → abort_fault
 *   │     motion 段    : Motion_Update()（IDLE catch-up，仅刷 odometry/航向，不驱动）
 *   │                    → Motion_StartXxx(段参数)   失败 → abort_fault
 *   │     记 deadline_base=now_ms；s_entered=true；返回（进入拍不驱动＝隔拍刹停间隙）
 *   │
 *   └─ 段已进入 ────────────────────► 推进唯一子服务 + 判完成/失活：
 *         FOLLOW_UNTIL : LineFollow_Update() → (PollElementEvents() & until_elements)!=0 → 完成
 *         motion 段    : Motion_Update()     → Motion_IsDone() → 完成
 *         完成 → finish_segment（FOLLOW_UNTIL 调 LineFollow_Stop 刹停；motion 已自刹停）
 *                cur++；cur>=count → DONE，否则下一拍进入下一段（隔拍刹停间隙）
 *         未完成时才查失活：FOLLOW_UNTIL 且 LOST → abort_fault；
 *                          timeout_ms>0 且 (now_ms−base)>=timeout_ms → abort_fault
 *
 * abort_fault：停当前活动子服务（motion 可能仍在驱动，必须显式 Motion_Stop）→ FAULT，此后静默。
 *
 * 安全（§8.1，软件可证）：init-to-safe = Setup/Start 不驱动、进入拍不驱动（底盘持上一段刹停态）；
 * 确定性停止 = Route_Stop 任意态刹停、段失败恒经子服务 Chassis_Stop；超时兜底 = 段级 timeout_ms
 * 是 route 唯一拥有的 liveness 保护；换向经停 = 段间隔拍刹停间隙避免两驱动源同拍并发。
 *
 * 单一所有者（§20.0/§20.4）：route 只做「事件掩码按位与 / 布尔查询」的编排判定，对任何数据变换
 * 零复做。route 唯一拥有段序 + 完成分派 + 段间交接 + 段级超时；时间经钩子参数注入，无 clock.h。
 */
#include "app/service/route/route.h"

#include "app/service/line_follow/line_follow.h"
#include "app/service/motion/motion.h"

#include <stddef.h>   /* NULL */

/* ---- 私有状态（route 唯一拥有：段序编排上下文） ------------------------- */

static const Route_Segment_T *s_segments;   /* 装配层注入的段表（生命周期由调用方保证） */
static uint8_t                s_count;       /* 段表长度 */
static Route_State            s_state;       /* 服务状态 */
static uint8_t                s_cur;         /* 当前段索引 */
static bool                   s_entered;     /* 当前段是否已进入（已启动子服务） */
static uint32_t               s_deadline_base; /* 当前段进入拍的 now_ms（段级超时基准） */

/* ---- 内部辅助 ----------------------------------------------------------- */

/* 停当前活动子服务并转 FAULT。进入即被拒的段无活动子服务，Stop 仍安全（幂等刹停）。 */
static void abort_fault(void)
{
    if (s_segments[s_cur].kind == ROUTE_SEG_FOLLOW_UNTIL) {
        LineFollow_Stop();   /* → IDLE + Chassis_Stop，刹车真值表保持 */
    } else {
        Motion_Stop();       /* motion 可能仍在驱动 → Chassis_Stop + IDLE */
    }
    s_state = ROUTE_FAULT;
}

/* 当前段完成：收尾当前子服务，推进到下一段或转 DONE。 */
static void finish_segment(void)
{
    if (s_segments[s_cur].kind == ROUTE_SEG_FOLLOW_UNTIL) {
        LineFollow_Stop();   /* → IDLE + Chassis_Stop，刹车保持 */
    }
    /* motion 段完成时已 Chassis_Stop + 静默，无需额外动作 */

    s_cur++;
    s_entered = false;
    if (s_cur >= s_count) {
        s_state = ROUTE_DONE;   /* 末段已刹停 */
    }
    /* 否则保持 RUNNING，下一拍进入下一段（隔拍刹停间隙） */
}

/* 进入当前段：启动唯一子服务，不驱动。返回 false = 段启动被拒（调用方转 fault）。 */
static bool enter_segment(uint32_t now_ms)
{
    const Route_Segment_T *seg = &s_segments[s_cur];
    bool ok;

    switch (seg->kind) {
    case ROUTE_SEG_FOLLOW_UNTIL:
        ok = LineFollow_Start();   /* 配置无效 → false */
        break;
    case ROUTE_SEG_STRAIGHT:
    case ROUTE_SEG_TURN:
    case ROUTE_SEG_ARC:
        /* odometry catch-up：进 motion 段前推一次 Motion_Update（此刻 motion IDLE/DONE →
         * 只排空 IMU + 刷新 odometry/航向，不驱动底盘），使 StartXxx 捕获的起点/航向基准
         * 反映 FOLLOW_UNTIL 期的真实位姿（FOLLOW_UNTIL 期无人推进 odometry 而冻结）。 */
        Motion_Update();
        if (seg->kind == ROUTE_SEG_STRAIGHT) {
            ok = Motion_StartStraight(seg->distance_mm, seg->heading_hold);
        } else if (seg->kind == ROUTE_SEG_TURN) {
            ok = Motion_StartTurn(seg->turn_deg);
        } else {
            ok = Motion_StartArc(seg->arc_radius_mm, seg->arc_deg);
        }
        break;
    default:
        ok = false;
        break;
    }

    if (ok) {
        s_deadline_base = now_ms;
        s_entered = true;
    }
    return ok;
}

/* 推进已进入的当前段一步，返回该段是否本拍完成。 */
static bool advance_segment(void)
{
    const Route_Segment_T *seg = &s_segments[s_cur];

    if (seg->kind == ROUTE_SEG_FOLLOW_UNTIL) {
        uint16_t events;
        LineFollow_Update();
        events = LineFollow_PollElementEvents();
        return (uint16_t)(events & seg->until_elements) != 0u;
    }

    Motion_Update();
    return Motion_IsDone();
}

/* ---- 公共接口 ----------------------------------------------------------- */

void Route_Setup(const Route_Segment_T *segments, uint8_t count)
{
    if (segments == NULL || count == 0u) {
        s_segments = NULL;
        s_count = 0u;
    } else {
        s_segments = segments;
        s_count = count;
    }
    s_state = ROUTE_IDLE;
    s_cur = 0u;
    s_entered = false;
    s_deadline_base = 0u;
}

void Route_Start(void)
{
    s_cur = 0u;
    s_entered = false;
    if (s_count == 0u) {
        s_state = ROUTE_DONE;   /* 空表 trivially 完成 */
    } else {
        s_state = ROUTE_RUNNING;
    }
    /* 不驱动底盘：驱动始于首个 Route_Update 的下一拍 */
}

void Route_Update(uint32_t now_ms)
{
    const Route_Segment_T *seg;

    if (s_state != ROUTE_RUNNING) {
        return;   /* IDLE/DONE/FAULT 完全静默（刹车真值表保持） */
    }

    if (!s_entered) {
        if (!enter_segment(now_ms)) {
            abort_fault();
        }
        return;   /* 进入拍不驱动（隔拍刹停间隙，交接确定性无正负跳变震荡） */
    }

    seg = &s_segments[s_cur];

    if (advance_segment()) {
        finish_segment();
        return;
    }

    /* 失活/超时检查（仅当本拍未完成） */
    if (seg->kind == ROUTE_SEG_FOLLOW_UNTIL &&
        LineFollow_GetState() == LINE_FOLLOW_LOST) {
        abort_fault();
        return;
    }
    if (seg->timeout_ms > 0u &&
        (uint32_t)(now_ms - s_deadline_base) >= seg->timeout_ms) {
        abort_fault();
    }
}

void Route_Stop(void)
{
    if (s_state == ROUTE_RUNNING && s_entered) {
        if (s_segments[s_cur].kind == ROUTE_SEG_FOLLOW_UNTIL) {
            LineFollow_Stop();
        } else {
            Motion_Stop();
        }
    }
    s_state = ROUTE_IDLE;
}

Route_State Route_GetState(void)
{
    return s_state;
}

bool Route_IsDone(void)
{
    return s_state == ROUTE_DONE;
}

void Route_GetTelemetry(Route_Telemetry_T *out)
{
    uint8_t idx;

    if (out == NULL) {
        return;
    }

    idx = s_cur;
    if (s_count == 0u) {
        idx = 0u;
    } else if (idx >= s_count) {
        idx = (uint8_t)(s_count - 1u);   /* DONE：cur 已越过末段，报告最后处理段 */
    }

    out->state = s_state;
    out->segment_index = idx;
    out->segment_count = s_count;
    out->current_kind = (s_count > 0u) ? s_segments[idx].kind : ROUTE_SEG_FOLLOW_UNTIL;
}
