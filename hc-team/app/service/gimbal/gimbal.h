/**
 * @file    gimbal.h
 * @brief   云台视觉瞄准服务（App Service，S05c，契约 §21.3）
 *
 * 视觉三闭环最后一环：把 uart_vision（坐标/握手编解码 Driver，S05a）与 vision_aim（像素误差→
 * 轴脉冲增量几何 Middleware，S05b）接线成完整链路——选题握手编排 + 像素瞄准收敛环 + 确定性安全停，
 * 步进总线走 Service→Driver 直连（emm42 组包 + stepmotor_uart 字节层），不依赖冻结的 App Task。
 *
 * 单一所有者（数据链 §8.2 / V26 / V22）：
 * - 死区/比例/步长限幅/极性/轴程限幅几何：唯一在 vision_aim，本服务逐拍把自持 cur_pulse 传入
 *   VisionAim_Map，绝不复算第二份；
 * - 轴累计绝对位置目标状态：唯一在本服务（vision_aim 纯函数不持位置），每拍无条件累加
 *   （绝对重发幂等、忙帧自愈；轴程限幅仍由 vision_aim 依 cur_pulse 施加）；
 * - 坐标编解码/CRC/分帧：唯一在 uart_vision；坐标时效判定（seq 停顿）：唯一在本服务；
 * - RPM 限幅+×10 与 0xAA/FC 封装：唯一在 emm42.c；双轴拼帧按轴序：在 gimbal_stepbus（无 dir/幅值拆分）。
 *
 * odometry 运动前馈（契约 §21.3 设计定案 2）：本轮不接线——预留接入点在 Gimbal_Update 的 AIMING 拍
 * （届时读 Odometry_GetPose() 折入瞄准），几何等有目标世界模型时以契约修订补入；本头不含 odometry.h。
 *
 * §8.1 电机安全：Init 不 enable（步进保持上电失能安全态）；步长/轴程限幅归 vision_aim；速度归 emm42；
 * 坐标失联/握手超时 → STOPPED 停止下发；Gimbal_Stop 确定性安全停可从正常控制流调用。
 */
#ifndef GIMBAL_H
#define GIMBAL_H

#include <stdbool.h>
#include <stdint.h>

#include "middleware/vision_aim/vision_aim.h"   /* VisionAim_Config_T / VISION_AIM_AXIS_COUNT（同层 Middleware，矩阵允许边） */

#ifdef __cplusplus
extern "C" {
#endif

/** 云台自门控控制周期（Clock_NowMs 无符号减法门控），沿用旧视觉跟踪 10ms 节奏。 */
#define GIMBAL_UPDATE_PERIOD_MS 10u

typedef enum {
    GIMBAL_STATE_IDLE = 0,      /* 未选题 */
    GIMBAL_STATE_HANDSHAKING,   /* 已发选题，等视觉确认帧 */
    GIMBAL_STATE_ARMING,        /* 确认到达，逐拍下发 enable/set-zero 建立原点 */
    GIMBAL_STATE_AIMING,        /* 运行期视觉像素闭环 */
    GIMBAL_STATE_STOPPED        /* 安全停 / 坐标超时 / 握手超时（需重新 SelectTopic） */
} Gimbal_State;

typedef struct {
    VisionAim_Config_T aim;      /* 透传 vision_aim：死区/kp/步长/极性/轴程限幅几何唯一所有者（本服务不复算） */
    uint16_t step_speed_rpm;     /* F1 快速位置预设速度（ARMING 期每轴设定一次）；emm42 限幅唯一所有者夹 ≤100，本服务不复夹 */
    uint32_t coord_timeout_ms;   /* AIMING 期坐标 seq 连续无进展达此时长 → STOPPED（安全停） */
    uint32_t ack_timeout_ms;     /* HANDSHAKING 期无确认帧达此时长 → STOPPED */
} Gimbal_Config_T;

typedef struct {
    Gimbal_State state;
    int32_t  cur_pulse[VISION_AIM_AXIS_COUNT];   /* 轴累计绝对脉冲目标（本服务唯一所有者；每拍无条件累加，忙帧自愈） */
    float    last_coord_x;
    float    last_coord_y;
    uint32_t last_coord_seq;                     /* 最近消费的坐标 seq */
    bool     axis_active[VISION_AIM_AXIS_COUNT]; /* 最近一拍该轴是否越死区（vision_aim active 透传） */
    float    last_error_px[VISION_AIM_AXIS_COUNT];   /* 最近一拍瞄准误差（vision_aim error_px 透传；死区拍亦有值） */
    int32_t  last_delta_pulse[VISION_AIM_AXIS_COUNT];/* 最近一拍输出增量（vision_aim delta 透传；死区拍为 0） */
    uint8_t  ack_main;                           /* 已确认主任务号 */
    uint8_t  ack_sub;                            /* 已确认子任务号 */
} Gimbal_Telemetry_T;

/**
 * 瞄准 PD 运行时调参子集（W8 契约 §30）：仅 kp/kd/死区/步长四组，逐轴。
 * center/sign/travel_limit 不在其内——几何/极性/行程是装配事实，运行中改动属事故面。
 */
typedef struct {
    float   kp[VISION_AIM_AXIS_COUNT];
    float   kd[VISION_AIM_AXIS_COUNT];
    float   deadband_px[VISION_AIM_AXIS_COUNT];
    int32_t max_step_pulse[VISION_AIM_AXIS_COUNT];
} Gimbal_AimTuning_T;

/**
 * 拷贝配置 + VisionAim_Init(&cfg.aim) + GimbalStepbus_Init；清状态 + cur_pulse=0 → IDLE。
 * 不发选题、不发移动、不 enable（安全起点，步进保持上电默认失能安全态）。
 * @param config NULL 视为误用，不写（同 pid/odometry 契约口径）。
 */
void Gimbal_Init(const Gimbal_Config_T *config);

/**
 * setup 期下发选题：UartVision_SendTopic 发 0xFF 选题帧 → 记待确认号 + 起始 ackseq → HANDSHAKING。
 * 任意态可调（重选题重置到 HANDSHAKING）。
 * @return 成功提交→true；底层 TX 忙→false（保持原态，不改状态）。
 */
bool Gimbal_SelectTopic(uint8_t main_task, uint8_t sub_task);

/**
 * 周期推进。末尾恒推进 GimbalStepbus_Service（消费 TX 完成 + drain/discard 步进 RX）；
 * 自门控 GIMBAL_UPDATE_PERIOD_MS：到期 → UartVision_Poll → 状态机：
 *   HANDSHAKING：确认帧 seq 进展且回显号匹配 → ARMING（cur_pulse=0）；超 ack_timeout_ms → STOPPED。
 *   ARMING：总线空时逐拍下发一帧 enable(X)/enable(Y)/preset(X)/preset(Y)/clear(X)/clear(Y)；
 *           六帧发完 → AIMING（preset 设 mode=ABSOLUTE+速度，clear 建立绝对坐标零点）。
 *   AIMING：坐标 seq 进展 → VisionAim_Map → 双轴无条件累加绝对目标 cur_pulse（轴程限幅经 vision_aim）
 *           → 总线空则一帧 0xAA 发双轴绝对目标（忙则跳过、下一拍发最新目标自愈）；
 *           seq 连续无进展达 coord_timeout_ms → STOPPED（短暂停顿保持 AIMING 静默不动）。
 */
void Gimbal_Update(void);

/**
 * 确定性安全停：停止下发、→STOPPED。可从正常控制流调用。步进保持使能（保持位置力矩＝云台安全态）。
 * cur_pulse 位置保留，不清零。
 */
void Gimbal_Stop(void);

/**
 * 运行时更新瞄准 PD 调参子集（能力：云台能在运行时换挡瞄准参数）。
 * 只改 cfg.aim 的 kp/kd/deadband/max_step 四组字段，经唯一应用点 VisionAim_Init 生效；
 * 不触碰运行状态（不清 prev_error、不改 state——换挡不打断收敛环）。
 * 调用方须保证值域（kp/kd/deadband>=0、max_step>=1）——外部输入的清洗归系统边界（tuning 层）。
 * @param tuning NULL 或从未成功 Init → 不写。
 */
void Gimbal_SetAimTuning(const Gimbal_AimTuning_T *tuning);

/**
 * 重发最近一次成功 SelectTopic 的题号（能力：云台能在安全停后重新发起上次握手）。
 * @return true=已重新提交（→HANDSHAKING）；false=从未选过题 / 未 Init / 底层 TX 忙。
 */
bool Gimbal_ReselectTopic(void);

Gimbal_State Gimbal_GetState(void);

/** 读出遥测快照。out==NULL 无副作用。 */
void Gimbal_GetTelemetry(Gimbal_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* GIMBAL_H */
