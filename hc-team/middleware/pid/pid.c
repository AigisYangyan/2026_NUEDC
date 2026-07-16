/*******************************************************************************
 * @file    pid.c
 * @brief   PID 控制模块实现
 *
 * 本文件实现当前项目使用的通用 PID 控制逻辑，并对外提供左右轮、角度、
 * 循迹等闭环入口函数。
 *
 * 功能范围：
 * - 管理项目内 PID 对象及默认参数。
 * - 提供位置式 / 增量式 PID 公式。
 * - 提供控制算法内生需要的约束：积分限幅、微分低通。
 * - 为任务调度层输出统一的控制量 `out`。
 *
 * 不负责的内容：
 * - 传感器采样与状态融合。
 * - 电机 / 步进驱动的实际输出执行。
 * - 针对单一赛题硬编码的大量分段经验规则。
 *
 * 设计说明：
 * 1. 继承旧版比赛代码里真正通用的控制构思，但不照搬强耦合的状态切换逻辑。
 * 2. PID 只做算法，不在中间层修复传感器突变，也不在这里补执行器死区。
 * 3. 闭环包装函数尽量保持薄，便于从任务层追踪“目标值 -> 反馈值 -> 输出值”。
 *******************************************************************************/

#include "pid.h"
#include <math.h>

 /* ---- 私有函数声明：实例初始化 ------------------------------------------ */

static void pid_InitVal(void);
static void pid_init_instance(PID_T* pid,
    float kp,
    float ki,
    float kd,
    float target,
    float limit);
static void pid_config_algorithm(PID_T* pid,
    float integral_limit,
    float d_filter_alpha);

/* ---- 私有函数声明：工程化辅助 ------------------------------------------ */

static float pid_clamp_symmetric(float value, float abs_limit);
static float pid_apply_first_order_filter(float current, float last, float alpha);
static float pid_get_integral_limit(const PID_T* pid);
static float pid_sanitize_output(float value, float fallback);

/* ---- 全局 PID 实例 ------------------------------------------------------ */

PID_T g_tAnglePID = { 0 };        /* 角度环 */
PID_T g_tLeftMotorPID = { 0 };    /* 左轮速度环 */
PID_T g_tRightMotorPID = { 0 };   /* 右轮速度环 */
PID_T g_tTrackPID = { 0 };        /* 循迹环 */
PID_T g_tPositionPID = { 0 };     /* 位置环 */

/* ---- 私有函数：实例初始化 ---------------------------------------------- */

/*******************************************************************************
 * @brief   初始化单个 PID 的基础参数与运行时状态
 *******************************************************************************/
static void pid_init_instance(PID_T* pid,
    float kp,
    float ki,
    float kd,
    float target,
    float limit)
{
    *pid = (PID_T){ 0 };
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->target = target;
    pid->limit = limit;
    pid->integral_limit = (limit > 0.0f) ? (limit * 3.5f) : 0.0f;
    pid->d_filter_alpha = 1.0f;
}

/*******************************************************************************
 * @brief   配置算法相关调节项
 *******************************************************************************/
static void pid_config_algorithm(PID_T* pid,
    float integral_limit,
    float d_filter_alpha)
{
    pid->integral_limit = integral_limit;
    pid->d_filter_alpha = d_filter_alpha;
}

/*******************************************************************************
 * @brief   初始化各个 PID 的默认参数
 *******************************************************************************/
static void pid_InitVal(void)
{
    /* 系统 - 角度环 */
    pid_init_instance(&g_tAnglePID, 0.004f, 0.0001f, 0.0015f, 0.0f, 0.4f);
    pid_config_algorithm(&g_tAnglePID, g_tAnglePID.integral_limit, 1.0f);

    /* 系统 - 左轮速度环 */
    pid_init_instance(&g_tLeftMotorPID, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f);
    pid_config_algorithm(&g_tLeftMotorPID, g_tLeftMotorPID.integral_limit, 1.0f);

    /* 系统 - 右轮速度环 */
    pid_init_instance(&g_tRightMotorPID, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f);
    pid_config_algorithm(&g_tRightMotorPID, g_tRightMotorPID.integral_limit, 1.0f);

    /* 系统 - 循迹环 */
    pid_init_instance(&g_tTrackPID, 0.00872f, 0.0f, 0.0030f, 0.0f, 0.6f);
    pid_config_algorithm(&g_tTrackPID, g_tTrackPID.integral_limit, 1.0f);

    /* 系统 - 位置环 */
    pid_init_instance(&g_tPositionPID, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f);
    pid_config_algorithm(&g_tPositionPID, g_tPositionPID.integral_limit, 1.0f);

}

/* ---- 私有函数：算法辅助 ------------------------------------------------ */

/*******************************************************************************
 * @brief   对称限幅
 *******************************************************************************/
static float pid_clamp_symmetric(float value, float abs_limit)
{
    if (abs_limit <= 0.0f) {
        return value;
    }

    if (value > abs_limit) {
        return abs_limit;
    }

    if (value < -abs_limit) {
        return -abs_limit;
    }

    return value;
}

/*******************************************************************************
 * @brief   一阶低通滤波
 * @note    作为微分项滤波器使用，alpha=1.0 表示不过滤
 *******************************************************************************/
static float pid_apply_first_order_filter(float current, float last, float alpha)
{
    if (alpha <= 0.0f) {
        return last;
    }

    if (alpha >= 1.0f) {
        return current;
    }

    return alpha * current + (1.0f - alpha) * last;
}

/*******************************************************************************
 * @brief   获取积分限幅
 *******************************************************************************/
static float pid_get_integral_limit(const PID_T* pid)
{
    if (pid->integral_limit > 0.0f) {
        return pid->integral_limit;
    }

    if (pid->limit > 0.0f) {
        return pid->limit * 3.5f;
    }

    return 0.0f;
}

/*******************************************************************************
 * @brief   输出有效值保护
 *******************************************************************************/
static float pid_sanitize_output(float value, float fallback)
{
    if (isnan(value) || isinf(value)) {
        if (isnan(fallback) || isinf(fallback)) {
            return 0.0f;
        }
        return fallback;
    }

    return value;
}

/* ---- 公共 API：生命周期 ------------------------------------------------ */

/*******************************************************************************
 * @brief   PID 模块初始化入口
 *******************************************************************************/
void pid_Init(void)
{
    pid_InitVal();
}

/* ---- 公共 API：业务闭环入口 -------------------------------------------- */

/*******************************************************************************
 * @brief   角度闭环控制函数[角度环]
 * @note    角度反馈尚未接入，本函数保留为占位闭环入口
 *******************************************************************************/
void pid_closeloop_angle(float _angle_target)
{
    (void)_angle_target;
    g_tAnglePID.current = 0.0f;
    g_tAnglePID.out = 0.0f;
}

/*******************************************************************************
 * @brief   电机闭环控制函数[左右轮速度环]
 * @note    使用增量式 PID
 *******************************************************************************/
void pid_closeloop_motor(float left_target_mps,
                         float right_target_mps,
                         float left_feedback_mps,
                         float right_feedback_mps,
                         float *p_left_out,
                         float *p_right_out)
{
    g_tLeftMotorPID.target = left_target_mps;
    g_tLeftMotorPID.current = left_feedback_mps;
    pid_formula_incremental(&g_tLeftMotorPID);
    pid_out_limit(&g_tLeftMotorPID);

    g_tRightMotorPID.target = right_target_mps;
    g_tRightMotorPID.current = right_feedback_mps;
    pid_formula_incremental(&g_tRightMotorPID);
    pid_out_limit(&g_tRightMotorPID);

    *p_left_out = g_tLeftMotorPID.out;
    *p_right_out = g_tRightMotorPID.out;
}

/*******************************************************************************
 * @brief   循迹闭环控制函数[循迹环]
 * @note    循迹反馈尚未接入，本函数保留为占位闭环入口
 *******************************************************************************/
void pid_closeloop_track(void)
{
    g_tTrackPID.current = 0.0f;
    g_tTrackPID.out = 0.0f;
}

/* ---- 公共 API：通用 PID 公式 ------------------------------------------- */

/*******************************************************************************
 * @brief   PID 输出限幅与有效值保护
 *******************************************************************************/
void pid_out_limit(PID_T* _tpPID)
{
    _tpPID->out = pid_sanitize_output(_tpPID->out, _tpPID->last_out);

    _tpPID->out = pid_clamp_symmetric(_tpPID->out, _tpPID->limit);
    _tpPID->last_out = _tpPID->out;
}

/*******************************************************************************
 * @brief   增量式 PID 公式
 * @note    仅保留算法内的微分滤波，不在这里修复采样数据异常。
 *******************************************************************************/
void pid_formula_incremental(PID_T* _tpPID)
{
    float raw_d_out;

    _tpPID->error = _tpPID->target - _tpPID->current;

    _tpPID->p_out = _tpPID->kp * (_tpPID->error - _tpPID->last_error);
    _tpPID->i_out = _tpPID->ki * _tpPID->error;

    raw_d_out = _tpPID->kd * (_tpPID->error
        - 2.0f * _tpPID->last_error
        + _tpPID->last2_error);
    _tpPID->d_out = pid_apply_first_order_filter(raw_d_out,
        _tpPID->last_d_out,
        _tpPID->d_filter_alpha);

    _tpPID->out += _tpPID->p_out + _tpPID->i_out + _tpPID->d_out;

    _tpPID->last2_error = _tpPID->last_error;
    _tpPID->last_error = _tpPID->error;
    _tpPID->last_d_out = _tpPID->d_out;
}

/*******************************************************************************
 * @brief   位置式 PID 公式
 * @note    保留简洁位置式结构，同时把积分限幅从硬编码改为可配置项
 *******************************************************************************/
void pid_formula_positional(PID_T* _tpPID)
{
    float raw_d_out;
    float integral_limit;

    _tpPID->error = _tpPID->target - _tpPID->current;
    _tpPID->integral += _tpPID->error;

    integral_limit = pid_get_integral_limit(_tpPID);
    _tpPID->integral = pid_clamp_symmetric(_tpPID->integral, integral_limit);

    _tpPID->p_out = _tpPID->kp * _tpPID->error;
    _tpPID->i_out = _tpPID->ki * _tpPID->integral;

    raw_d_out = _tpPID->kd * (_tpPID->error - _tpPID->last_error);
    _tpPID->d_out = pid_apply_first_order_filter(raw_d_out,
        _tpPID->last_d_out,
        _tpPID->d_filter_alpha);

    _tpPID->out = _tpPID->p_out + _tpPID->i_out + _tpPID->d_out;

    _tpPID->last_error = _tpPID->error;
    _tpPID->last_d_out = _tpPID->d_out;
}
