/**
 * @file    motion.h
 * @brief   语义运动服务（App Service 层）——「pose + 语义目标 → 底盘速度」编排的唯一所有者。
 *
 * 抽象（底盘能做什么）：
 * - 走一段指定前进距离后停（可选按 IMU 航向纠偏保持直线）。
 * - 原地转到一个相对角度后停。
 * - 以定半径走一段定角圆弧后停（双轮速度比前馈 + 航向误差修正）。
 * - 随时确定性停止。
 * - 报告当前运动是否完成，以及位姿/状态遥测。
 *
 * 隐藏：
 * - 用了哪些 Driver（Encoder/IMU 只读快照 + IMU 排空）、Middleware（odometry/pid）、
 *   Service（chassis），以及里程计上下文、航向保持外环、运动状态机与起点/基准参考。
 *
 * 分层与所有权（AGENTS.md §3.4 / §8.1 / §8.2；phase4 计划表 §15）：
 * - 本服务是 IMU FIFO 排空节奏（Imu_Update）的唯一所有者（激活期独占，类比 chassis 独占
 *   Encoder_Update）；是里程计消费节奏（一次性消费 total_pulses）与语义运动状态机的唯一所有者。
 * - 不复做：编码器采样/elapsed（chassis）、输出限幅/slew/换向/超时/刹车（motor.c 经 chassis）、
 *   脉冲→距离换算与 yaw 符号（odometry cfg）、yaw unwrap（heading.c）、底盘速度环增益（装配层）。
 * - 确定性停止：到位/Stop 走 Chassis_Stop 后转 IDLE/DONE 静默，刹车真值表保持（§8.1）。
 *
 * 调用前置条件：
 * - 装配层已完成 Clock/Motor/Encoder/IMU 的 Init，且已设 Chassis 速度环增益（否则底盘不出力）；
 *   建议一次运行开始前 Imu_ZeroYaw() 使首航向≈0。本服务 Init 不发电机命令、不采样。
 * - 头文件不暴露任何 Driver 类型（左右/传感器语义用本服务与 odometry 的自持类型）。
 */
#ifndef HC_TEAM_APP_SERVICE_MOTION_MOTION_H
#define HC_TEAM_APP_SERVICE_MOTION_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 运动状态机。IDLE/DONE 时底盘静默（不泵内环，刹车真值表保持——确定性驻停）。 */
typedef enum {
    MOTION_IDLE = 0,   /* 无原语 */
    MOTION_STRAIGHT,   /* 直行中 */
    MOTION_TURN,       /* 原地转中 */
    MOTION_ARC,        /* 圆弧行进中 */
    MOTION_DONE,       /* 原语完成（已 Chassis_Stop） */
} Motion_State;

/** 运动服务配置。由装配层填写后经 Motion_Init 按值拷入。 */
typedef struct {
    /* 里程计标定：透传给 odometry；脉冲→距离、yaw 符号的单一所有者仍是 Odometry_Config。 */
    float mm_per_pulse;        /* >0，实测标定，无默认值 */
    float heading_sign;        /* +1 或 −1，实测标定 */
    /* 运动基速。 */
    float straight_speed_mps;  /* 直行基速（前进为正） */
    float turn_speed_mps;      /* 原地转单轮速度幅值上限（>0） */
    float arc_speed_mps;       /* 圆弧圆心线速度基速（前进为正，>0；仅圆弧原语用） */
    /* 圆弧几何：轮距是本服务新增的单一所有者（§19.0），仅用于圆弧前馈内外轮速比，
       绝不构成第二个航向权威——圆弧完成与修正仍读 odometry 连续航向（IMU 源）。 */
    float track_width_mm;      /* 轮距（mm，>0，实测标定） */
    /* 直行航向保持外环（位置式 PID：输入航向误差 deg → 输出差速修正 m/s）。 */
    float hold_kp;
    float hold_ki;
    float hold_kd;
    float hold_diff_limit_mps; /* 纠偏差速对称限幅 = 该 PID out_limit（限幅唯一所有者 = 此 cfg） */
    /* 定角转（比例控制 deg→m/s）。 */
    float turn_kp;
    /* 到位判据。 */
    float straight_tol_mm;     /* 直行到位容差（>0） */
    float turn_tol_deg;        /* 转角到位容差（>0） */
} Motion_Config_T;

/** 一次性读出的运动遥测。 */
typedef struct {
    Motion_State state;
    float x_mm;         /* 当前 odometry 位姿 */
    float y_mm;
    float heading_deg;  /* 连续多圈航向角 */
    float target;       /* 当前原语目标：STRAIGHT=距离 mm；TURN=相对角 deg；ARC=圆心角 deg；IDLE/DONE=0 */
    float progress;     /* 已完成量：STRAIGHT=已行进 mm；TURN/ARC=已转过 deg；IDLE/DONE=0 */
} Motion_Telemetry_T;

/**
 * @brief  初始化/重置服务：里程计 Init（cfg 透传）+ 航向保持 PID Init + 状态 IDLE。
 * @param  cfg  服务配置；cfg==NULL 视为误用（按零配置处理，同 odometry 契约口径）。
 * @note   不发电机命令、不采样、不调 Encoder_Update/Imu_Update（初始安全态）。
 *         前置：装配层已 Chassis_Init 且已设 Chassis 速度环增益。
 */
void Motion_Init(const Motion_Config_T *cfg);

/**
 * @brief  开始一段直行。
 * @param  distance_mm   目标前进距离（mm）；<=0 → 返回 false 保持当前态。
 * @param  heading_hold  true = 按 IMU 航向纠偏保持直线；false = 双轮等速开环直行。
 * @return true 已进入 STRAIGHT。
 * @note   捕获当前位姿为起点参考、清航向保持 PID 史。
 */
bool Motion_StartStraight(float distance_mm, bool heading_hold);

/**
 * @brief  开始一次原地定角转。
 * @param  relative_deg  相对当前航向的目标角（度）；+ = CCW（左转），− = CW（右转）；
 *                       ==0 → 返回 false。
 * @return true 已进入 TURN。
 * @note   捕获当前航向为基准。用 odometry 去卷连续航向闭环，非裸角速度积分。
 */
bool Motion_StartTurn(float relative_deg);

/**
 * @brief  开始一段定半径圆弧（前进圆弧）。
 * @param  radius_mm  圆弧半径（车中心，mm，>0）。约束 radius_mm >= track_width_mm/2——
 *                    否则内轮将反向，属原地掰弯（TURN 语义）而非前进圆弧；不满足返回 false。
 * @param  arc_deg    圆心角（度，带号）；+ = CCW（左转，航向递增），− = CW（右转）；==0 → false。
 * @return true 已进入 ARC。
 * @note   前馈由 radius+track_width 定内外轮速比；行进中读 odometry 连续航向做误差修正。
 *         完成判据 = 已扫过 |arc_deg|（航向驱动，IMU 权威）。捕获当前航向为基准、
 *         当前位姿为路径长基准、清航向修正 PID 史（复用航向保持 PID 实例）。
 */
bool Motion_StartArc(float radius_mm, float arc_deg);

/**
 * @brief  推进一步运动（事件驱动，无自门控——每次调用一次控制迭代）。
 * @note   任意态：Imu_Update()（本服务独占排空）→ 读 Encoder/IMU 快照 → 以 total_pulses 差值
 *         一次性消费推进 Odometry_Update → 取位姿。STRAIGHT/TURN：算控制律 → Chassis_SetTargetMps
 *         → 到位则 Chassis_Stop + DONE；末尾恒推进 Chassis_Update()（内环自门控 10ms）。
 *         IDLE/DONE：不设目标、不泵内环（刹车真值表保持，确定性驻停）。
 */
void Motion_Update(void);

/** @brief 确定性停止：Chassis_Stop + 状态 IDLE。随时可从正常控制流调用（§8.1）。 */
void Motion_Stop(void);

/** @brief 当前运动状态。 */
Motion_State Motion_GetState(void);

/** @brief 当前原语是否完成（state==MOTION_DONE）。 */
bool Motion_IsDone(void);

/** @brief 复制运动遥测快照。out==NULL 无副作用。 */
void Motion_GetTelemetry(Motion_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_APP_SERVICE_MOTION_MOTION_H */
