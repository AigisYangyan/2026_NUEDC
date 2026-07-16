/**
 * @file    speed_loop.h
 * @brief   DEBUG 速度环最小原子化测试服务接口
 *
 * 本模块负责组织编码器采样、左右轮增量式 PID、电机输出与 VOFA 调参链路。
 *
 * 功能范围：
 * - 管理 SpeedLoop 调试目标值与遥测变量
 * - 提供进入/退出运行项时的一次性安全动作
 * - 提供 10ms 采样、10ms 控制、20ms 遥测接口
 *
 * 设计约定：
 * - 目标速度单位统一为 m/s
 * - PID 核心复用现有 pid_closeloop_motor()
 * - 本模块只做编排，不重写控制算法
 *
 * 使用方式：
 * 1. 系统初始化阶段调用 `SpeedLoop_Init()`
 * 2. 进入运行项时调用 `SpeedLoop_Enter()`
 * 3. 运行中由任务组按 10ms/10ms/20ms 节拍调用采样、控制和遥测接口
 * 4. 离开运行项时调用 `SpeedLoop_Exit()`
 */

#ifndef __SPEED_LOOP_H__
#define __SPEED_LOOP_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 对外接口 ----------------------------------------------------------- */

/**
 * @brief 速度环调试服务初始化
 * @note  该接口只做一次性注册动作，例如 VOFA 通道绑定与初值准备。
 */
void SpeedLoop_Init(void);

/**
 * @brief 进入速度环调试运行项
 * @note  该接口不做模块二次初始化，只做目标值/PID 运行时状态清零。
 */
void SpeedLoop_Enter(void);

/**
 * @brief 退出速度环调试运行项
 * @note  退出时归零并刹车，确保离开 RUNNING 页面后电机停止。
 */
void SpeedLoop_Exit(void);

/**
 * @brief 10ms 编码器采样入口
 */
void SpeedLoop_Sample10ms(void);

/**
 * @brief 10ms 速度环控制入口
 */
void SpeedLoop_Control10ms(void);

/**
 * @brief 20ms VOFA 遥测入口
 */
void SpeedLoop_Telemetry20ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __SPEED_LOOP_H__ */
