/**
 * @file    line_follow.h
 * @brief   循迹外环服务（App Service 层）——「循迹环触发 + 多环级联」的唯一所有者。
 *
 * 抽象（循迹功能能做什么）：
 * - 沿黑线行驶（白底黑线）、丢线后有界恢复、恢复超时安全停车、
 *   报告状态与误差遥测、运行时调外环增益。
 *
 * 隐藏：
 * - 用了哪些 Driver/Middleware/Service、控制周期门控、外环 PID 上下文、
 *   丢线策略状态、状态机内部布局。
 *
 * 分层与所有权：
 * - 误差量化唯一所有者 = middleware/track_error（本服务是其第一个消费者，不复算）；
 * - 位序左右唯一修正点 = 配置项 bit0_is_left，透传给 TrackError（gray.h 位序警告的落点）；
 * - 差速修正限幅唯一所有者 = 外环 PID 配置（= diff_limit_mps）；
 * - 轮速闭环、电机保护归 chassis 服务及其下游既有所有者，本服务只发目标。
 *
 * 多环级联：LineFollow_Update() 推进外环（10ms 门控）并在每次调用末尾恒推进
 * Chassis_Update()（内环自带门控）。Task 只需泵一个 Update。
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

/** 服务配置。pitch_mm 与 bit0_is_left 是机械/接线事实（H2 实测与机械定案后给定）。 */
typedef struct {
    float    pitch_mm;          /* 相邻探头中心间距，必须 > 0 */
    bool     bit0_is_left;      /* 位图 bit0 是否为车左端（位序唯一修正点） */
    float    base_speed_mps;    /* 巡线基速 */
    float    diff_limit_mps;    /* 差速修正限幅，必须 > 0（外环 PID out_limit） */
    float    recovery_error_mm; /* 丢线回退误差幅值（建议 ≈ 2.7 × pitch_mm） */
    uint32_t lost_timeout_ms;   /* 丢线恢复上限，超时 → LOST 停车 */
} LineFollow_Config_T;

/** 一次性读出的服务遥测。 */
typedef struct {
    uint16_t dark_bitmap;   /* 最近一次采样位图 */
    float    error_mm;      /* 最近一拍使用的误差（含回退误差） */
    float    diff_cmd_mps;  /* 最近一拍差速修正 c（left=base+c, right=base−c） */
    LineFollow_State state;
} LineFollow_Telemetry_T;

/** 初始化/重置服务：存配置、外环 PID 归零、回 IDLE。不动底盘。config 必须非空。 */
void LineFollow_Init(const LineFollow_Config_T *config);

/** 设置外环 PID 增益（位置式，输入误差 mm，输出差速 m/s），立即生效。 */
void LineFollow_SetGains(float kp, float ki, float kd);

/**
 * @brief  启动循迹。
 * @return true = 进入 TRACKING；false = 配置无效（pitch_mm ≤ 0 或
 *         diff_limit_mps ≤ 0），保持 IDLE。
 */
bool LineFollow_Start(void);

/**
 * @brief  推进循迹环。允许被任意更快的节奏调用，外环按 10ms 门控；
 *         每次调用末尾恒推进 Chassis_Update()（内环自带门控）。
 *         IDLE/LOST 态不采样、不发目标，只透传内环推进。
 */
void LineFollow_Update(void);

/**
 * @brief  确定性停止：回 IDLE 并调用 Chassis_Stop()。
 * @note   刹车态持续语义同 Chassis_Stop（维持到下一次到期的内环 tick）。
 */
void LineFollow_Stop(void);

/** 当前状态。 */
LineFollow_State LineFollow_GetState(void);

/** 复制当前遥测快照。out 必须非空。 */
void LineFollow_GetTelemetry(LineFollow_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* LINE_FOLLOW_H */
