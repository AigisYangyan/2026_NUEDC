/**
 * @file    motor.h
 * @brief   双路直流电机 Driver 的最小公共接口。
 *
 * 抽象：
 * - 按电机 ID 提供初始化、安全输出、周期推进和刹车能力。
 *
 * 隐藏：
 * - 方向引脚映射、PWM compare 口径、换向死区、命令超时和内部状态机。
 *
 * 分层：
 * - 仅属于 Driver 执行层；不暴露 PID、编码器或任何上层状态。
 */

#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 带符号输出口径：-1000..1000 对应 0%..100% 占空比 */
#define MOTOR_OUTPUT_MAX   1000

/* ---- 类型定义 ----------------------------------------------------------- */

    /* 电机索引：仅用于选择左右轮执行通道。 */
    typedef enum {
        MOTOR_LEFT = 0,
        MOTOR_RIGHT,
        MOTOR_COUNT
    } Motor_Id;

    /* ---- 公开 API ----------------------------------------------------------- */

    /** 上电安全态初始化：方向清零、compare 清零并启动 PWM 计数器。 */
    void Motor_Init(void);

    /**
     * @brief  设置电机目标输出。
     * @param  id      左/右电机索引。
     * @param  output  带符号归一化输出，合法范围为 [-MOTOR_OUTPUT_MAX, MOTOR_OUTPUT_MAX]。
     * @return true=接受并刷新命令时间；false=参数越界或 ID 非法。
     */
    bool Motor_SetOutput(Motor_Id id, int16_t output);

    /**
     * @brief 推进 Motor 私有状态机。
     * @param elapsed_ms 与上次推进之间的真实毫秒数；0 表示本次不推进。
     */
    void Motor_Update(uint32_t elapsed_ms);

    /** 主动刹车：H 桥进入刹车真值表，PWM compare 立即归零。 */
    void Motor_Brake(Motor_Id id);

    /** 全部电机刹车 */
    void Motor_BrakeAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H__ */
