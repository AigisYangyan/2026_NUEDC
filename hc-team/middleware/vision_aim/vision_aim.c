/**
 * @file    vision_aim.c
 * @brief   视觉像素误差 → 云台轴脉冲增量 纯几何映射实现（S05b，契约 §21.2）
 *
 * 唯一状态是一份私有配置副本（Init 拷入）；Map 之外无可变数据流状态——
 * delta 只是 (coord, cur_pulse, config) 的纯函数，不跨拍记账、不持墙钟。
 * 轴累计物理位置由调用方持有并逐拍传入（见 vision_aim.h 单一所有者声明）。
 *
 * 数据变换链（逐轴，data-chain §8.2）：
 *   coord(px) − center(px) = error(px, float, 不截断)
 *     → 死区：|error|<=deadband → delta 0 / active false
 *     → 比例步长：step = |error|*kp，floor 至 1（越死区最小走 1 脉冲，杜绝触发不动）
 *     → 步长限幅：clamp 至 max_step_pulse（每拍幅值封顶）
 *     → 极性：按 error 符号取正负，再乘 sign[axis]（唯一极性开关）
 *     → 轴程限幅：依 cur_pulse，令 cur+delta 不越 ±travel_limit（<=0 不限幅）
 *
 * 前置条件（不加无依据防御代码）：max_step_pulse >= 1、kp/deadband >= 0、sign ∈ {+1,-1}。
 */
#include "middleware/vision_aim/vision_aim.h"

#include <stddef.h>

/* ---- 私有配置副本（唯一 static） --------------------------------------- */

static VisionAim_Config_T s_cfg;
static bool s_has_cfg = false;

/* ---- 私有几何辅助（项目惯例：各模块自持局部 clamp，不依赖不存在的共享库） ---- */

static float vision_aim_abs_f32(float value)
{
    return (value >= 0.0f) ? value : -value;
}

/*
 * 轴程绝对软限位：令 cur+delta 落在 [-limit, +limit]。
 * limit<=0 → 不限幅，原样返回。cur 已越界时返回把它拉回边界的增量（安全方向）——
 * 该回拉增量的幅值可超过 max_step_pulse 的每拍封顶，属有意的「安全方向优先」；
 * 常态闭环下调用方 faithful 回灌 cur+delta（永在界内）不会走到此分支。
 */
static int32_t vision_aim_clamp_to_travel(int32_t cur, int32_t delta, int32_t limit)
{
    int32_t next;

    if (limit <= 0) {
        return delta;
    }

    next = cur + delta;
    if (next > limit) {
        return limit - cur;
    }
    if (next < -limit) {
        return (-limit) - cur;
    }
    return delta;
}

/*
 * 单轴映射：coord → 本拍脉冲增量。写出 error_px 与 active，返回 delta。
 */
static int32_t vision_aim_map_axis(VisionAim_Axis axis,
                                   float coord,
                                   int32_t cur_pulse,
                                   float *error_out,
                                   bool *active_out)
{
    float error = coord - s_cfg.center_px[axis];
    float mag_err = vision_aim_abs_f32(error);
    float step_f;
    float max_step_f;
    int32_t mag;
    int32_t delta;

    *error_out = error;

    /* 死区：越不过 → 本拍不动 */
    if (mag_err <= s_cfg.deadband_px[axis]) {
        *active_out = false;
        return 0;
    }
    *active_out = true;

    /* 比例步长 + floor 1 + 步长限幅（顺序承旧 visionhdl_step_from_error） */
    step_f = mag_err * s_cfg.kp[axis];
    if (step_f < 1.0f) {
        step_f = 1.0f;
    }
    max_step_f = (float)s_cfg.max_step_pulse[axis];
    if (step_f > max_step_f) {
        step_f = max_step_f;
    }
    mag = (int32_t)step_f;   /* step_f>=1.0（前置 max_step>=1）→ mag>=1 */

    /* 极性：先按误差符号，再乘 sign（唯一极性开关） */
    delta = (error >= 0.0f) ? mag : -mag;
    if (s_cfg.sign[axis] < 0) {
        delta = -delta;
    }

    /* 轴程限幅 */
    return vision_aim_clamp_to_travel(cur_pulse, delta, s_cfg.travel_limit_pulse[axis]);
}

/* ---- 公开 API ---------------------------------------------------------- */

void VisionAim_Init(const VisionAim_Config_T *config)
{
    if (config == NULL) {
        return;
    }
    s_cfg = *config;
    s_has_cfg = true;
}

void VisionAim_Map(float coord_x, float coord_y,
                   int32_t cur_x_pulse, int32_t cur_y_pulse,
                   VisionAim_Result_T *out)
{
    if ((out == NULL) || (s_has_cfg == false)) {
        return;
    }

    out->delta_pulse[VISION_AIM_AXIS_X] =
        vision_aim_map_axis(VISION_AIM_AXIS_X, coord_x, cur_x_pulse,
                            &out->error_px[VISION_AIM_AXIS_X],
                            &out->active[VISION_AIM_AXIS_X]);

    out->delta_pulse[VISION_AIM_AXIS_Y] =
        vision_aim_map_axis(VISION_AIM_AXIS_Y, coord_y, cur_y_pulse,
                            &out->error_px[VISION_AIM_AXIS_Y],
                            &out->active[VISION_AIM_AXIS_Y]);
}
