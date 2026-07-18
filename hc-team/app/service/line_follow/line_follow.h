/**
 * @file    line_follow.h
 * @brief   循迹外环服务（App Service 层）——「循迹环触发 + 多环级联」的唯一所有者。
 *
 * 抽象（循迹功能能做什么）：
 * - 沿黑线行驶（白底黑线）、丢线后有界恢复、恢复超时安全停车、
 *   报告状态与误差遥测、运行时调外环增益；
 * - 按弯道曲率自动调整巡航基速（直道提速、入弯减速）；
 * - 报告经过的循迹几何元素事件（断线/横线/左岔/右岔的确认上升沿）。
 *
 * 隐藏：
 * - 用了哪些 Driver/Middleware/Service、控制周期门控、外环 PID 上下文、
 *   丢线策略状态、速度规划斜坡状态、元素检测置信状态、状态机内部布局。
 *
 * 分层与所有权（§8.2 单一所有者）：
 * - 误差量化唯一所有者 = middleware/track_error（本服务是其消费者，不复算）；
 * - 基速调制唯一所有者 = middleware/speed_plan（本服务只在合成点用其输出替换固定基速）；
 * - 循迹元素几何检测唯一所有者 = middleware/track_elements（与 track_error 并列消费同一位图）；
 * - 位序左右唯一修正点 = 配置项 bit0_is_left，透传给 track_error 与 track_elements 各一次
 *   （并列独立应用，非级联二次反转）；
 * - 差速修正限幅唯一所有者 = 外环 PID 配置（= diff_limit_mps）；
 * - 轮速闭环、电机保护归 chassis 服务及其下游既有所有者，本服务只发目标。
 *
 * 多环级联：LineFollow_Update() 推进外环（10ms 门控），并在 TRACKING/RECOVERING
 * 期间推进 Chassis_Update()（内环自带门控）——Task 只需泵一个 Update。
 * IDLE/LOST 完全静默（不采样、不发目标、不推进内环），Chassis_Stop 的刹车真值表
 * 因此得以保持；底盘另作他用时由使用者直接泵 Chassis_Update()。
 *
 * 调用前置条件：System 装配完成 Clock/Motor/Encoder 初始化，且 Chassis_Init 已执行。
 */
#ifndef LINE_FOLLOW_H
#define LINE_FOLLOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 服务状态。 */
typedef enum {
    LINE_FOLLOW_IDLE = 0,   /* 未启动：不采样、不发底盘目标 */
    LINE_FOLLOW_TRACKING,   /* 正常循迹 */
    LINE_FOLLOW_RECOVERING, /* 丢线恢复：按记忆方向回退误差找线 */
    LINE_FOLLOW_LOST        /* 恢复超时：底盘已停，静默至 Stop/Start */
} LineFollow_State;

/**
 * 循迹几何元素事件位（bit = 1u<<kind，与 middleware/track_elements 的
 * TrackElement_Kind 同位序；实现内 _Static_assert 锁定一致，头不暴露「用了哪个
 * Middleware」——§3.4）。用作 element_enable_mask / PollElementEvents /
 * confirmed_elements 的位号。
 */
typedef enum {
    LINE_FOLLOW_ELEM_GAP          = 1u << 0, /* 断线：有效位全 0 */
    LINE_FOLLOW_ELEM_FULL_BAR     = 1u << 1, /* 横线：触双边且深色路数达阈（十字/终点，不分语义） */
    LINE_FOLLOW_ELEM_BRANCH_LEFT  = 1u << 2, /* 左岔 / 左直角 */
    LINE_FOLLOW_ELEM_BRANCH_RIGHT = 1u << 3  /* 右岔 / 右直角 */
} LineFollow_Element;

/** 服务配置。几何/接线事实（H2 实测与机械定案后给定），标定量无默认值。 */
typedef struct {
    float    pitch_mm;             /* 相邻探头中心间距，必须 > 0 */
    bool     bit0_is_left;         /* 位图 bit0 是否为车左端（位序唯一修正点，两消费者共用） */

    /* 巡航速度规划（middleware/speed_plan）——替换原固定 base_speed_mps */
    float    straight_speed_mps;   /* 直道巡航基速上限（|error|≈0 目标），> 0（原 base_speed 语义） */
    float    min_speed_mps;        /* 入弯最低基速下限，0 ≤ min ≤ straight */
    float    curve_error_mm;       /* 达 min_speed 的误差幅值阈，> 0；≤0 退化=不降速（基速恒 straight） */
    float    accel_mps_per_s;      /* 提速斜坡速率上限（m/s per second），> 0 */
    float    decel_mps_per_s;      /* 降速斜坡速率上限（m/s per second），> 0 */

    /* 差速外环 */
    float    diff_limit_mps;       /* 差速修正限幅，必须 > 0（外环 PID out_limit） */

    /* 丢线恢复（lost_line） */
    float    recovery_error_mm;    /* 丢线回退误差幅值（建议 ≈ 2.7 × pitch_mm） */
    uint32_t lost_timeout_ms;      /* 丢线恢复上限，超时 → LOST 停车 */

    /* 循迹元素检测（middleware/track_elements） */
    uint8_t  full_bar_min_count;   /* 判 FULL_BAR 的最小深色路数（1..12） */
    uint8_t  branch_min_span;      /* 判 BRANCH 的最小深色跨度（1..12） */
    uint8_t  element_confirm_ticks;/* 连续满足多少拍置确认（去毛刺）；0 归一化为 1 */
    uint16_t element_enable_mask;  /* bit(LineFollow_Element)=1 启用该检测器；0 = 全不检测 */
} LineFollow_Config_T;

/** 一次性读出的服务遥测。 */
typedef struct {
    uint16_t dark_bitmap;       /* 最近一次采样位图 */
    float    error_mm;          /* 最近一拍使用的误差（含回退误差） */
    float    base_speed_mps;    /* 最近一拍规划基速（speed_plan 输出，合成 base±diff 的 base） */
    float    diff_cmd_mps;      /* 最近一拍差速修正 c（left=base+c, right=base−c） */
    uint16_t confirmed_elements;/* 当前元素确认电平掩码（LineFollow_Element 位；非事件、不清） */
    LineFollow_State state;
} LineFollow_Telemetry_T;

/** 初始化/重置服务：存配置、外环 PID 归零、速度规划/元素检测复位、回 IDLE。不动底盘。config 必须非空。 */
void LineFollow_Init(const LineFollow_Config_T *config);

/** 设置外环 PID 增益（位置式，输入误差 mm，输出差速 m/s），立即生效。 */
void LineFollow_SetGains(float kp, float ki, float kd);

/**
 * @brief  启动循迹。速度规划复位至 min_speed（安全起步），元素检测置信/事件清零。
 * @return true = 进入 TRACKING；false = 配置无效（pitch_mm ≤ 0 或
 *         diff_limit_mps ≤ 0），保持 IDLE。
 */
bool LineFollow_Start(void);

/**
 * @brief  推进循迹环。允许被任意更快的节奏调用，外环按 10ms 门控；
 *         到期一次原子读位图，并列驱动误差量化与元素检测；速度规划按曲率调制基速；
 *         TRACKING/RECOVERING 期间在末尾推进 Chassis_Update()（内环自带门控）；
 *         IDLE/LOST 完全静默（不采样、不发目标、不推进内环、不产生元素事件，刹车保持）。
 */
void LineFollow_Update(void);

/**
 * @brief  确定性停止：回 IDLE 并调用 Chassis_Stop()。
 * @note   IDLE 态本服务不再推进内环，机械刹车持续保持直至重新 Start
 *         （或使用者自行泵 Chassis_Update 接管底盘）。
 */
void LineFollow_Stop(void);

/** 当前状态。 */
LineFollow_State LineFollow_GetState(void);

/** 复制当前遥测快照。out 必须非空。 */
void LineFollow_GetTelemetry(LineFollow_Telemetry_T *out);

/**
 * @brief  取出并清空循迹元素确认的上升沿事件（段切换触发源）。
 * @return LineFollow_Element 位的掩码：bit=1 表示该元素最近一拍由未确认转确认；随即清零。
 *         无新事件 → 0。与遥测 confirmed_elements（电平、不清）读不同源，互不清除。
 * @note   IDLE/LOST 静默期不采样，故不产生新事件。
 */
uint16_t LineFollow_PollElementEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* LINE_FOLLOW_H */
