/**
 * @file    vision_aim.h
 * @brief   视觉像素误差 → 云台轴脉冲增量 纯几何映射（Middleware，S05b）
 *
 * 纯算法 Middleware：不含 Driver/App/DL-HAL 依赖，不持墙钟，不做时效判定。
 * 把视觉像素坐标（float32 x/y）映射成云台 X/Y 双轴的有符号脉冲增量（int32/轴）。
 *
 * 单一所有者（数据链 §8.2）：本模块是「死区 / 比例增益 / 微分增益 / 步长限幅 / 极性 / 轴程限幅几何」
 * 的唯一所有者。任何第二处的极性反转、限幅、缩放或滤波都会与本层抵消/冲突，属违规。
 *
 * 不拥有的东西：
 * - 轴的累计物理位置「状态」——归调用方（S05c 云台服务）持有，逐拍经 Map 传入；
 *   本层只拥有「轴程限幅的几何公式」，不拥有「当前位置这个数」。
 * - 上一拍误差「状态」（prev_error，D 项所需）——同样归调用方持有、逐拍传入；
 *   本层保持纯函数，不跨拍记账（同 cur_pulse 先例）。
 * - 视觉帧编解码 / 坐标时效判定——归 driver/uart_vision（S05a）与其消费者（S05c 消费 seq）。
 * - 脉冲→方向/幅值拆分与电机执行——归 S05c/emm42。
 *
 * 口径约定：
 * - 输入 coord = 视觉坐标系原始像素（S05a 原样透传），本层不做单位换算。
 * - error_px = coord − center（float，不截断；符号 = 视觉坐标系原始方向）。
 * - de = error_px − prev_error（调用方持有的上一拍误差逐拍传入）。控制律 = 位置式 PD：
 *   raw = kp*error + kd*de；方向/幅值由 raw 决定。**无积分、无微分滤波**——坐标已由上位机
 *   Kalman 滤波，本层禁二重滤波（§8.2，同 IMU 内置 Kalman 先例）；步进 cur_pulse 累加即积分器，
 *   无需控制器 I。kd=0 时逐位退化回纯比例。
 * - 输出 delta_pulse = 有符号相对脉冲增量（对齐 emm42 pulses+dir 协议；全仓无「度→脉冲」所有者，
 *   故本层不产出角度）。
 */
#ifndef VISION_AIM_H
#define VISION_AIM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VISION_AIM_AXIS_X = 0,
    VISION_AIM_AXIS_Y,
    VISION_AIM_AXIS_COUNT
} VisionAim_Axis;

typedef struct {
    float   center_px[VISION_AIM_AXIS_COUNT];        /* 图像中心像素（视觉分辨率决定，调用方给） */
    float   deadband_px[VISION_AIM_AXIS_COUNT];      /* 逐轴死区半径(>=0)；|误差|<=死区 → 不动 */
    float   kp[VISION_AIM_AXIS_COUNT];               /* 比例增益 像素→脉冲(>=0) */
    float   kd[VISION_AIM_AXIS_COUNT];               /* 微分增益 像素误差速率→脉冲(>=0)；默认 0=退化纯 P。
                                                        无微分滤波：坐标已上位机 Kalman，二重滤波违 §8.2 */
    int32_t max_step_pulse[VISION_AIM_AXIS_COUNT];   /* 每拍步长幅值上限(>0)；越死区后 |delta|∈[1,max_step] */
    int8_t  sign[VISION_AIM_AXIS_COUNT];             /* 极性唯一开关(+1/-1)；机械/坐标系装反在此吸收 */
    int32_t travel_limit_pulse[VISION_AIM_AXIS_COUNT]; /* 轴程绝对软限位 ±值；<=0 表示不限幅 */
} VisionAim_Config_T;

typedef struct {
    float   error_px[VISION_AIM_AXIS_COUNT];         /* 坐标-中心(float,不截断,符号=视觉坐标系原始方向) */
    bool    active[VISION_AIM_AXIS_COUNT];           /* 单一语义：误差是否在死区外。注意轴程饱和拍
                                                        active 可为 true 而 delta 被限幅为 0——
                                                        「本拍是否真的位移」请由调用方以 delta_pulse!=0 判断 */
    int32_t delta_pulse[VISION_AIM_AXIS_COUNT];      /* 本拍有符号脉冲增量(已含极性+步长限幅+轴程限幅) */
} VisionAim_Result_T;

/**
 * 拷贝配置到私有 static。无副作用。
 * @param config 为 NULL → 忽略（保持已有配置不变）。
 */
void VisionAim_Init(const VisionAim_Config_T *config);

/**
 * 纯函数：逐轴把像素坐标映射为本拍脉冲增量（位置式 PD）。
 *
 * 逐轴处理：error = 坐标 − 中心 → 死区门控（|error|<=deadband → delta 0/active false，
 *          但 out.error_px 恒 = error 供调用方存 prev） → de = error − prev_error
 *          → raw = kp*error + kd*de → 方向 = sign(raw)、幅值 = |raw|（floor 1, clamp max_step）
 *          → 极性 sign → 轴程限幅（依 cur_pulse，令 cur+delta 不越 ±travel_limit） → delta_pulse。
 * 无积分、无微分滤波（坐标已上位机 Kalman；kd=0 时逐位退化回纯 P）。
 *
 * @param coord_x/coord_y 视觉坐标原样像素（S05a 透传）。
 * @param cur_x_pulse/cur_y_pulse 调用方持有的该轴当前累计脉冲位置。
 * @param prev_error_x/prev_error_y 调用方持有的该轴上一拍 error_px；首拍令其 = 本拍 error 则 de=0（无首拍 D 冲击）。
 * @param out 结果输出；为 NULL 或从未成功 Init（无配置）→ 不写出。
 */
void VisionAim_Map(float coord_x, float coord_y,
                   int32_t cur_x_pulse, int32_t cur_y_pulse,
                   float prev_error_x, float prev_error_y,
                   VisionAim_Result_T *out);

#ifdef __cplusplus
}
#endif

#endif /* VISION_AIM_H */
