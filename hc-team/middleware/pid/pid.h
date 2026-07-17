#ifndef __PID_H__
#define __PID_H__

/**
 * @file    pid.h
 * @brief   PID 纯算法模块（Middleware）——调用者持有上下文
 *
 * @details
 * 模块职责：
 * - 提供单实例 PID 求解能力：配置（增益/限幅/微分滤波）、复位、
 *   增量式/位置式两种更新公式、内部分量遥测读出。
 * - 不持有任何模块级实例：`Pid_T` 上下文由调用者静态分配并显式传入。
 *
 * 设计约定（AGENTS.md §3.3）：
 * 1. 纯算法：不感知电机/编码器/传感器类型，不做单位换算、方向修正或采样修复。
 * 2. 输入按参数传入（目标值/反馈值同量纲，由调用者保证），输出按返回值给出。
 * 3. `Pid_T` 的运行时字段是模块私有状态，调用者不得直接读写；
 *    唯一读出口是 `Pid_GetTelemetry()`，唯一写入口是本头的配置/更新函数。
 * 4. 限幅语义：输出对称限幅 `out_limit`（<=0 不限幅）；积分限幅
 *    `integral_limit`（<=0 时按 `out_limit*3.5` 推导）；NaN/Inf 输出回退到上次有效输出。
 */

/* ---- 类型定义 ----------------------------------------------------------- */

/** PID 配置。由调用者填写后经 Pid_Init() 拷入上下文。 */
typedef struct
{
    float kp;              /* 比例增益 */
    float ki;              /* 积分增益 */
    float kd;              /* 微分增益 */
    float out_limit;       /* 输出对称限幅，<=0 表示不限幅 */
    float integral_limit;  /* 积分累计限幅，<=0 表示按 out_limit*3.5 推导 */
    float d_filter_alpha;  /* 微分一阶低通系数 (0,1]，1.0 表示不过滤 */
} Pid_Config_T;

/** 内部分量遥测快照（调参观测用）。 */
typedef struct
{
    float out;             /* 最近一次限幅后的输出 */
    float p_out;           /* 比例分量 */
    float i_out;           /* 积分分量 */
    float d_out;           /* 滤波后的微分分量 */
} Pid_Telemetry_T;

/**
 * PID 上下文。定义暴露仅为让调用者静态分配；
 * cfg 之外的字段全部为模块私有运行时状态，禁止直接读写。
 */
typedef struct
{
    Pid_Config_T cfg;      /* 当前配置（经 Pid_Init/Pid_SetGains/Pid_SetLimits 写入） */

    /* -- 私有运行时状态，调用者不得触碰 ------------------------------------ */
    float error;           /* 当前误差 */
    float last_error;      /* 上一次误差 */
    float last2_error;     /* 上上次误差 */
    float integral;        /* 积分累计（位置式） */
    float out;             /* 当前输出（限幅后） */
    float last_out;        /* 上一次输出（NaN 回退源） */
    float p_out;           /* 比例分量 */
    float i_out;           /* 积分分量 */
    float d_out;           /* 滤波后微分分量 */
    float last_d_out;      /* 上一次滤波后微分分量 */
} Pid_T;

/* ---- 公共 API ------------------------------------------------------------ */

/**
 * @brief 初始化上下文：清零全部运行时状态并拷入配置。
 * @param pid    调用者持有的上下文，必须有效。
 * @param config 初始配置，必须有效；按值拷入，调用后可释放。
 */
void Pid_Init(Pid_T *pid, const Pid_Config_T *config);

/**
 * @brief 清零运行时状态（误差史/积分/输出/滤波史），保留全部配置。
 * @note  用于闭环启停边界，避免历史误差在重启后放大输出。
 */
void Pid_Reset(Pid_T *pid);

/** @brief 在线更新三增益，不影响运行时状态与限幅配置。 */
void Pid_SetGains(Pid_T *pid, float kp, float ki, float kd);

/**
 * @brief 在线更新限幅配置。
 * @param out_limit      输出对称限幅，<=0 不限幅。
 * @param integral_limit 积分限幅，<=0 按 out_limit*3.5 推导。
 */
void Pid_SetLimits(Pid_T *pid, float out_limit, float integral_limit);

/**
 * @brief 增量式更新一步。
 * @param target   目标值；@param feedback 反馈值（与目标同量纲，由调用者保证）。
 * @return 限幅后的执行量（累加型输出）。
 */
float Pid_UpdateIncremental(Pid_T *pid, float target, float feedback);

/**
 * @brief 位置式更新一步（含积分累计与积分限幅）。
 * @return 限幅后的执行量。
 */
float Pid_UpdatePositional(Pid_T *pid, float target, float feedback);

/** @brief 读出最近一次更新的输出与 P/I/D 分量（只读，不改变状态）。 */
void Pid_GetTelemetry(const Pid_T *pid, Pid_Telemetry_T *out_telemetry);

#endif
