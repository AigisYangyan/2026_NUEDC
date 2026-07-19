/**
 * @file    move_profile.c
 * @brief   定长运动速度剖面实现——按距离参数化的梯形前馈（纯函数，无状态）。
 *
 * 数据链（§8.2 登记）：
 *   odometry 位姿 → 调用者(motion.c)自算已行进距离 dist_done_mm [mm]
 *   → MoveProfile_Speed(dist, target) [距离→前馈速度，本模块唯一所有者]
 *   → 前馈基速 [m/s，夹在 [0,cruise]] → 调用者合成 Chassis_SetTargetMps。
 *
 * 剖面（按距离，非按时间）：加速段 v=sqrt(start^2 + 2*accel*s) 从起步速升起；
 *   匀速段夹到 cruise；减速段 v=sqrt(2*decel*rem) 随剩余距离收敛到 0。三者取 min，
 *   每拍以实测距离重算——自带位置反馈，无需第二个纵向 PID。
 *
 * 唯一所有权：定长运动速度剖面这一变换。不采样、不碰 Chassis、不做脉冲→距离换算
 *   （内部 mm→m 仅量纲对齐）。
 */
#include "middleware/move_profile/move_profile.h"

#include <math.h>
#include <stddef.h>

float MoveProfile_Speed(const MoveProfile_Config_T *cfg,
                        float dist_done_mm, float target_mm)
{
    float s_m;      /* 已行进（m） */
    float rem_m;    /* 剩余（m） */
    float v_acc;    /* 加速段限速 */
    float v_dec;    /* 减速段限速 */
    float v;

    if (cfg == NULL) {
        return 0.0f;
    }
    if (target_mm <= 0.0f) {
        return 0.0f;                 /* 无效目标 */
    }
    if (dist_done_mm < 0.0f) {
        dist_done_mm = 0.0f;         /* 负位移视为起点 */
    }
    if (dist_done_mm >= target_mm) {
        return 0.0f;                 /* 到位/越界：不驱动，由调用者发 Chassis_Stop */
    }

    s_m = dist_done_mm / 1000.0f;                    /* mm → m，仅量纲对齐 */
    rem_m = (target_mm - dist_done_mm) / 1000.0f;    /* > 0（上面已排除 >= target） */

    /* 加速段：起步速兜底，随已行进距离升起。 */
    v_acc = sqrtf(cfg->start_mps * cfg->start_mps + 2.0f * cfg->accel_mps2 * s_m);
    /* 减速段：随剩余距离收敛到 0。 */
    v_dec = sqrtf(2.0f * cfg->decel_mps2 * rem_m);

    /* 三段取最小：匀速上限 ∧ 加速段 ∧ 减速段。 */
    v = cfg->cruise_mps;
    if (v_acc < v) {
        v = v_acc;
    }
    if (v_dec < v) {
        v = v_dec;
    }
    if (v < 0.0f) {
        v = 0.0f;                    /* 保险：配置异常时不出负速 */
    }
    return v;
}
