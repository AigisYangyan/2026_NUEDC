/**
 * @file    encoder.c
 * @brief   两轮差速底盘编码器采样与速度换算模块
 *
 * @details
 * 模块职责：
 * 1. 从 Board GPIO 读取左右轮原始计数的一致快照。
 * 2. 仅做一次安装方向修正，计算单周期脉冲增量。
 * 3. 使用调用者传入的真实 elapsed_ms 将脉冲增量换算为线速度（m/s）。
 * 4. 以按值快照形式向 Service 暴露数据。
 *
 * 数据与单位约定：
 * - 原始计数：硬件相位决定的有符号累计脉冲。
 * - total_pulses：统一成“车辆前进为正”后的累计脉冲。
 * - delta_pulses：本次与上次快照的差值。
 * - speed_mps：使用真实 elapsed_ms 计算，单位为 m/s。
 *
 * 依赖：
 * - driver/board_gpio/board_gpio.h 提供的 BoardGpio_GetEncoderRawSnapshot()
 *
 * 注意：
 * - 累计计数差值使用 uint32_t 模运算处理 int32_t 回绕，再转回有符号差值。
 * - P2 不增加滤波；若未来需要，只能由一个明确 Middleware 模块实现。
 * - P1 复审：u32_mod_i32/i64_narrow_i32 消除所有 implementation-defined 与溢出 UB。验证通过。
 */

#include "driver/encoder/encoder.h"
#include "driver/board_gpio/board_gpio.h"
#include <stddef.h>
#include <string.h>

#define ENCODER_PI 3.1416f

/* ---- 私有板级常量（修改时必须伴随测量依据） ----------------------------- */

static const float s_wheel_circumference_mm = ENCODER_PI * 68.6f;
static const float s_miu = 1.0f;
static const int32_t s_ppr = 1560;

/* 安装方向修正：左轮 -1，右轮 +1。这是全链路唯一方向修正点。 */
static const int8_t s_direction_sign[ENCODER_COUNT] = { -1, 1 };

/* ---- 私有状态 ------------------------------------------------------------- */

static bool s_initialized = false;
static int32_t s_previous_raw[ENCODER_COUNT] = { 0, 0 };
static Encoder_Snapshot s_snapshot = { 0 };

/* ---- 显式模 2^32 映射（消除 implementation-defined 转换）----------------- */

/**
 * @brief 将 uint32_t 按 2^32 模运算映射到 int32_t。
 *
 * 映射规则：u ∈ [0, INT32_MAX] → u；u ∈ [INT32_MIN_u, UINT32_MAX] → u - 2^32。
 * 使用 memcpy 按位重解释，是 C 标准中唯一不依赖 implementation-defined 行为
 * 的类型双关方式。在所有 2's-complement 目标（MSPM0、x86、ARM、C23 强制）上，
 * 结果与直接 (int32_t) 转换一致，但语义已显式化且无 UB。
 */
static int32_t u32_mod_i32(uint32_t u)
{
    int32_t s;
    memcpy(&s, &u, sizeof(s));
    return s;
}

/**
 * @brief 将 int64_t 窄化为 int32_t，保留低 32 位。
 *
 * (uint32_t)wide 由 C 标准定义为 wide mod 2^32（端序无关）。
 * 再通过 u32_mod_i32(memcpy) 得到 int32_t 按位重解释。
 * 仅用于溢出安全中间乘积的截断（乘积值在 int32_t 模 2^32 范围内）。
 */
static int32_t i64_narrow_i32(int64_t wide)
{
    return u32_mod_i32((uint32_t)wide);
}

/* ---- 新 API 实现 ---------------------------------------------------------- */

/**
 * @brief 将原始计数修正为车辆前进方向，并计算安全差值。
 * @param prev_raw  上次原始计数。
 * @param curr_raw  当前原始计数。
 * @param sign      安装方向修正符号。
 * @param out_delta 输出修正后的有符号差值。
 * @return 修正后的当前累计脉冲。
 */
static int32_t encoder_correct_and_delta(int32_t prev_raw,
                                         int32_t curr_raw,
                                         int8_t sign,
                                         int32_t *out_delta)
{
    uint32_t prev_u = (uint32_t)prev_raw;
    uint32_t curr_u = (uint32_t)curr_raw;
    /* Unsigned subtraction naturally wraps modulo 2^32. */
    uint32_t diff_u = curr_u - prev_u;
    int32_t raw_delta = u32_mod_i32(diff_u);

    /* int64_t intermediate prevents signed overflow UB when raw_delta == INT32_MIN
     * and sign == -1, or when curr_raw == INT32_MIN and sign == -1.
     * i64_narrow_i32 truncates back to int32_t via well-defined memcpy. */
    *out_delta = i64_narrow_i32((int64_t)raw_delta * (int64_t)sign);
    return i64_narrow_i32((int64_t)curr_raw * (int64_t)sign);
}

/**
 * @brief 将修正后的脉冲增量换算为 m/s。
 */
static float encoder_delta_to_mps(int32_t delta, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0u || s_ppr == 0) {
        return 0.0f;
    }

    /* distance_mm = delta * (wheel_circ_mm / ppr) * miu */
    /* speed_mps = distance_mm / 1000.0f / (elapsed_ms / 1000.0f) */
    /*           = delta * wheel_circ_mm * miu / (ppr * elapsed_ms) */
    return (float)delta * s_wheel_circumference_mm * s_miu /
           ((float)s_ppr * (float)elapsed_ms);
}

void Encoder_Init(void)
{
    BoardEncoderRawSnapshot raw = { 0, 0 };

    s_initialized = false;

    if (BoardGpio_GetEncoderRawSnapshot(&raw) == true) {
        s_previous_raw[ENCODER_LEFT] = raw.left;
        s_previous_raw[ENCODER_RIGHT] = raw.right;
        s_snapshot.total_pulses[ENCODER_LEFT]  = i64_narrow_i32((int64_t)raw.left  * (int64_t)s_direction_sign[ENCODER_LEFT]);
        s_snapshot.total_pulses[ENCODER_RIGHT] = i64_narrow_i32((int64_t)raw.right * (int64_t)s_direction_sign[ENCODER_RIGHT]);
        for (int i = 0; i < ENCODER_COUNT; i++) {
            s_snapshot.delta_pulses[i] = 0;
            s_snapshot.speed_mps[i] = 0.0f;
        }
        s_initialized = true;
    } else {
        for (int i = 0; i < ENCODER_COUNT; i++) {
            s_previous_raw[i] = 0;
            s_snapshot.total_pulses[i] = 0;
            s_snapshot.delta_pulses[i] = 0;
            s_snapshot.speed_mps[i] = 0.0f;
        }
    }

}

bool Encoder_Update(uint32_t elapsed_ms)
{
    BoardEncoderRawSnapshot raw = { 0, 0 };

    if (elapsed_ms == 0u) {
        return false;
    }

    if (BoardGpio_GetEncoderRawSnapshot(&raw) == false) {
        return false;
    }

    if (s_initialized == false) {
        /* 首拍：只建立基线，增量和速度为零。 */
        s_previous_raw[ENCODER_LEFT] = raw.left;
        s_previous_raw[ENCODER_RIGHT] = raw.right;
        s_snapshot.total_pulses[ENCODER_LEFT]  = i64_narrow_i32((int64_t)raw.left  * (int64_t)s_direction_sign[ENCODER_LEFT]);
        s_snapshot.total_pulses[ENCODER_RIGHT] = i64_narrow_i32((int64_t)raw.right * (int64_t)s_direction_sign[ENCODER_RIGHT]);
        for (int i = 0; i < ENCODER_COUNT; i++) {
            s_snapshot.delta_pulses[i] = 0;
            s_snapshot.speed_mps[i] = 0.0f;
        }
        s_initialized = true;
        return true;
    }

    {
        int32_t delta_left;
        int32_t delta_right;
        int32_t corrected_left = encoder_correct_and_delta(s_previous_raw[ENCODER_LEFT],
                                                           raw.left,
                                                           s_direction_sign[ENCODER_LEFT],
                                                           &delta_left);
        int32_t corrected_right = encoder_correct_and_delta(s_previous_raw[ENCODER_RIGHT],
                                                            raw.right,
                                                            s_direction_sign[ENCODER_RIGHT],
                                                            &delta_right);
        s_previous_raw[ENCODER_LEFT] = raw.left;
        s_previous_raw[ENCODER_RIGHT] = raw.right;
        s_snapshot.total_pulses[ENCODER_LEFT] = corrected_left;
        s_snapshot.total_pulses[ENCODER_RIGHT] = corrected_right;
        s_snapshot.delta_pulses[ENCODER_LEFT] = delta_left;
        s_snapshot.delta_pulses[ENCODER_RIGHT] = delta_right;
        s_snapshot.speed_mps[ENCODER_LEFT] = encoder_delta_to_mps(delta_left, elapsed_ms);
        s_snapshot.speed_mps[ENCODER_RIGHT] = encoder_delta_to_mps(delta_right, elapsed_ms);
    }

    return true;
}

void Encoder_GetSnapshot(Encoder_Snapshot *out)
{
    if (out == NULL) {
        return;
    }

    *out = s_snapshot;
}
