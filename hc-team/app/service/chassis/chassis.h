/**
 * @file    chassis.h
 * @brief   底盘速度环服务（App Service 层）——「速度环触发」框架的唯一所有者。
 *
 * 抽象（底盘能做什么）：
 * - 按目标轮速（m/s）闭环行驶、确定性刹停、报告目标/反馈/输出遥测。
 *
 * 隐藏：
 * - 用了哪些 Driver/Middleware、控制周期门控、PID 上下文与编码器快照。
 *
 * 分层与所有权：
 * - 本服务是编码器采样节奏的唯一所有者（elapsed 由单调毫秒钟无符号减法得出）。
 * - 输出限幅唯一所有者是 PID 配置（±1000 PWM）；slew、换向过零+死区、
 *   命令超时归零、刹车真值表全部在 Motor Driver 状态机内，本服务不复做。
 *
 * 调用前置条件：
 * - System 装配层已完成 Clock/Motor/Encoder 的 Init；本服务 Init 不发电机命令。
 */
#ifndef CHASSIS_H
#define CHASSIS_H

#ifdef __cplusplus
extern "C" {
#endif

/** 底盘左右侧（驾驶员视角，沿车头前进方向看）。 */
typedef enum {
    CHASSIS_SIDE_LEFT = 0,
    CHASSIS_SIDE_RIGHT,
    CHASSIS_SIDE_COUNT
} Chassis_Side;

/** 一次性读出的服务遥测：目标/反馈（m/s）与 PID 输出（±1000 PWM）。 */
typedef struct {
    float target_mps[CHASSIS_SIDE_COUNT];
    float feedback_mps[CHASSIS_SIDE_COUNT];
    float pid_out[CHASSIS_SIDE_COUNT];
} Chassis_Telemetry_T;

/** 初始化/重置服务：目标清零、PID 增益归零并清运行史、周期基准复位。不发电机命令。 */
void Chassis_Init(void);

/**
 * @brief  设置单侧速度环 PID 增益（运行时可调参）。
 * @param  side  左/右侧；非法值忽略。
 * @param  kp/ki/kd  增量式 PID 增益，立即生效。
 */
void Chassis_SetSpeedGains(Chassis_Side side, float kp, float ki, float kd);

/**
 * @brief  设置左右轮目标速度。
 * @param  left_mps/right_mps  目标轮速（m/s），符号 = 前进为正。
 * @note   不做目标限幅：无实测最大轮速依据，输出由 PID out_limit 界定
 *         （已知空缺，实车标定后如需限幅由本服务唯一补上）。
 */
void Chassis_SetTargetMps(float left_mps, float right_mps);

/**
 * @brief  推进速度环。允许被任意更快的节奏调用，内部按控制周期（10ms）门控；
 *         不足周期直接返回且零硬件访问。到期执行：编码器采样(真实 elapsed)
 *         → 双轮增量 PID → 电机输出 → Motor 状态机推进。
 */
void Chassis_Update(void);

/** 确定性停止：目标清零 + PID 复位 + 全部电机刹车。可随时从正常控制流调用。 */
void Chassis_Stop(void);

/**
 * @brief  复制当前遥测快照。
 * @param  out 调用者拥有的输出结构；必须非空。
 */
void Chassis_GetTelemetry(Chassis_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* CHASSIS_H */
