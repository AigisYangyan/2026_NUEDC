#ifndef __PID_H__
#define __PID_H__

/**
 * @file    pid.h
 * @brief   PID 控制模块对外接口定义
 *
 * @details
 * 模块职责：
 * - 提供项目内通用的 PID 数据结构、位置式/增量式计算公式。
 * - 统一管理左右轮、角度、循迹等控制环的 PID 对象。
 * - 提供少量算法内配置项，便于在不引入重型控制策略的前提下完成调参。
 *
 * 设计约定：
 * 1. PID 核心公式保持通用，不把业务特有的赛题经验规则硬编码进公式本体。
 * 2. PID 只保留算法内生能力：比例、积分、微分、积分限幅与可选微分滤波。
 * 3. 传感器突变、采样修复、执行器死区补偿应分别放在数据源层和执行层处理。
 * 4. 执行层（电机、步进驱动）只消费 `out`，传感器采样与业务状态判断由上层完成。
 */

/* ---- 类型定义 ----------------------------------------------------------- */

typedef struct
{
    float kp;                    /* 比例 */
    float ki;                    /* 积分 */
    float kd;                    /* 微分 */
    float target;                /* 目标值 */
    float current;               /* 当前反馈值 */
    float out;                   /* 执行量 */
    float limit;                 /* 输出限幅值 */

    float error;                 /* 当前误差 */
    float last_error;            /* 上一次误差 */
    float last2_error;           /* 上上次误差 */
    float last_out;              /* 上一次输出 */
    float integral;              /* 积分累计 */
    float p_out;                 /* 比例分量输出 */
    float i_out;                 /* 积分分量输出 */
    float d_out;                 /* 微分分量输出 */
    float last_d_out;            /* 上一次滤波后的微分分量 */

    float integral_limit;        /* 积分累计限幅，<=0 表示按输出限幅推导 */
    float d_filter_alpha;        /* 微分一阶低通系数，1.0 表示不过滤 */
} PID_T;

/* ---- 全局 PID 实例 ------------------------------------------------------ */

extern PID_T g_tAnglePID;
extern PID_T g_tLeftMotorPID;
extern PID_T g_tRightMotorPID;
extern PID_T g_tTrackPID;
extern PID_T g_tPositionPID;

/* ---- 生命周期与业务闭环接口 -------------------------------------------- */

void pid_Init(void);
void pid_closeloop_angle(float _angle_target);
/**
 * @brief 更新左右轮增量式速度 PID，并按值返回控制量。
 * @param left_target_mps 左轮目标速度，单位 m/s。
 * @param right_target_mps 右轮目标速度，单位 m/s。
 * @param left_feedback_mps 左轮反馈速度，单位 m/s。
 * @param right_feedback_mps 右轮反馈速度，单位 m/s。
 * @param p_left_out 调用者拥有的左轮输出存储，调用时必须有效。
 * @param p_right_out 调用者拥有的右轮输出存储，调用时必须有效。
 * @note 输出沿用 PID 实例的对称限幅，当前电机实例范围为 -1000..1000。
 */
void pid_closeloop_motor(float left_target_mps,
                         float right_target_mps,
                         float left_feedback_mps,
                         float right_feedback_mps,
                         float *p_left_out,
                         float *p_right_out);
void pid_closeloop_track(void);

/* ---- 通用 PID 公式与辅助函数 ------------------------------------------- */

void pid_formula_positional(PID_T * _tpPID);
void pid_formula_incremental(PID_T * _tpPID);
void pid_out_limit(PID_T * _tpPID);

#endif
