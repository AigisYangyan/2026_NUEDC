/**
 * @file    task1.h
 * @brief   TASK1 — 100x100 正方形巡线行驶服务对外接口
 *
 * @details
 * 模块职责：
 * - 编排一个完整的“直线循迹 + 直角转弯”有限状态机
 * - 满足设定圈数后自动停车，输出 DONE 状态
 *
 * 状态机：
 *   IDLE            初始态；等待 Task1_Enter 置 ARM
 *   STRAIGHT        直线段；使用 Track PID (速度环) 循迹
 *   BEFORE_ANGLE    角点补偿；检测到直角后继续直行固定时长
 *   ANGLE           原地差速左转；用陀螺仪 Gz 积分到目标角度
 *   DONE            已跑完目标圈数；刹车并锁定
 *
 * 圈计数：
 *   每完成 1 次 ANGLE→STRAIGHT 切换记 1 个 FLAG；
 *   每 4 个 FLAG = 1 圈；lap >= TASK1_TARGET_LAPS 进入 DONE。
 *
 * 设计约定：
 * 1. 本模块只调度上层状态机，不重写 PID / 电机 / 编码器底层
 * 2. 直线段用本任务持有的左右轮 Pid_T 做双轮速度环，循迹误差→速度差
 * 3. 控制层生成带符号输出后，通过 Motor Driver 执行换向/死区/超时保护
 * 4. 陀螺仪积分只取陀螺仪 Gz (°/s)，不使用卡尔曼解算
 * 5. 进入角点时机：灰度低 4 位亮 (左半有线) + 高 4 位全灭 (右半无线)
 *
 * 单位与口径：
 * - 速度    : m/s
 * - 角度    : ° (deg)
 * - 角速度  : °/s
 *
 * 依赖：
 * - driver/motor, driver/encoder（陀螺仪驱动待接入）
 * - middleware/pid（本任务持有的 Pid_T 左右轮实例，Pid_UpdateIncremental）
 * - app/tasks/track_follow (Track_UpdateSample / Calculate_Track_Error)
 *
 * 使用方式：
 * 1. SysInit 中调用 Task1_Init() 一次
 * 2. RUN_ENTRY_TASK1 进入时通过 Task1_Enter() 启动；退出时 Task1_Exit()
 * 3. 任务组以 10ms 采样 / 10ms 控制 / 20ms 遥测 调度下列接口
 */

#ifndef __TASK1_H__
#define __TASK1_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 状态枚举 (仅用于遥测/调试) ---------------------------------------- */

typedef enum {
    TASK1_STATE_IDLE          = 0,
    TASK1_STATE_STRAIGHT      = 1,
    TASK1_STATE_BEFORE_ANGLE  = 2,
    TASK1_STATE_ANGLE         = 3,
    TASK1_STATE_DONE          = 4
} Task1_State_e;

/* ---- 生命周期 ----------------------------------------------------------- */

/** 一次性初始化 (VOFA 通道绑定、状态复位) */
void Task1_Init(void);

/** 进入运行项：复位状态机并 ARM 到 STRAIGHT */
void Task1_Enter(void);

/** 退出运行项：刹车并回到 IDLE */
void Task1_Exit(void);

/* ---- 周期任务入口 ------------------------------------------------------- */

/** 10ms 采样：编码器 + 灰度 + 陀螺仪 (Gz, 待接入) */
void Task1_Sample10ms(void);

/** 10ms 控制：状态机推进 + 电机输出 */
void Task1_Control10ms(void);

/** 20ms 遥测：VOFA + OLED 运行页刷新由 UI 组负责 */
void Task1_Telemetry20ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __TASK1_H__ */
