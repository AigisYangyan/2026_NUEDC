/**
 * @file    task1.c
 * @brief   TASK1 — 100x100 正方形巡线行驶服务实现
 *
 * 本文件实现 TASK1 状态机：
 *   STRAIGHT (循迹 PID) -> BEFORE_ANGLE (0.5s 距离补偿) ->
 *   ANGLE (MPU6050 角速度积分原地左转) -> STRAIGHT -> ... -> DONE
 *
 * 核心数据流：
 *   采样层 (10ms):
 *     - Encoder_GetSnapshot()         : 读取 TaskGroups 唯一采样所有者更新的电机速度 (m/s)
 *     - Track_UpdateSample()          : 8 路灰度位图
 *     - MPU6050_Read_Gyro()           : 获取 Gz (°/s)，本状态机只需要 Gz
 *
 *   控制层 (10ms):
 *     - STRAIGHT:     循迹误差 -> 左右目标速度差 -> pid_closeloop_motor
 *     - BEFORE_ANGLE: 固定目标速度 -> pid_closeloop_motor
 *     - ANGLE:        速度环输出经 Motor_SetOutput 下发到 Driver
 *     - DONE:         Motor_BrakeAll
 *
 *   遥测层 (20ms):
 *     - VOFA 推送 state/lap/flag/angle_deg
 *     - 调用 vofa_run 发送
 */

#include <stdint.h>
#include <stdbool.h>
#include "app/tasks/task1/task1.h"

#include "app/scheduler/vofa_register.h"
#include "driver/motor/motor.h"
#include "driver/encoder/encoder.h"
#include "driver/MPU6050/mpu6050.h"
#include "driver/clock/clock.h"
#include "driver/uart_vofa/uart_vofa.h"
#include "middleware/pid/pid.h"

#include "app/tasks/track_follow/track_follow.h"

#include <math.h>

/* ========================================================================== */
/* 可调参数 (所有阈值均用 #define，烧录前可按需调整)                          */
/* ========================================================================== */

/* -------- 圈数目标 -------- */
#define TASK1_TARGET_LAPS              1u         /* 目标圈数：每圈 4 个 FLAG */
#define TASK1_FLAGS_PER_LAP            4u         /* 每圈 FLAG 数 (正方形=4) */

/* -------- 直线段 -------- */
#define TASK1_BASE_SPEED_MPS           0.30f      /* 直线段基础速度 (m/s) */
#define TASK1_TRACK_KP                 0.0040f    /* 循迹误差→速度差系数；正值代表
                                                    error>0 时(右侧权重大=车偏左) 左轮加速、右轮减速 */
#define TASK1_SPEED_DIFF_LIMIT_MPS     0.15f      /* 循迹纠正最大速度差 */

/* -------- 距离补偿 (BEFORE_ANGLE) -------- */
#define TASK1_BEFORE_ANGLE_MS          500u       /* 补偿时长 (ms)，题目要求 0.5s */

/* -------- 原地转弯 (ANGLE) -------- */
/*
 * 转弯输出口径 = m/s (速度环)。
 * 理由:
 *  1. 编码器是带符号正交解码 (see mspm0_runtime.c)，能给出负速度
 *  2. 速度环能抑制电池电压波动带来的转速漂移，圈与圈之间更一致
 *  3. 进入 ANGLE 前 task1_enter_state() 会调 task1_brake_all() 清空 PID 历史，
 *     避免增量 PID 带着前进积分残留。
 */
#define TASK1_TURN_SPEED_MPS           0.30f      /* 原地差速转弯时单轮的速度幅值 (m/s) */
#define TASK1_TURN_TARGET_DEG          85.0f      /* 陀螺仪积分到该值算转弯完成；
                                                     留 5° 余量补偿惯性过冲 */
#define TASK1_TURN_GATE_MS             150u       /* 转弯起步前的 gyro 稳定时间 */

/* -------- 角点识别 (STRAIGHT → BEFORE_ANGLE) -------- */
/* 低 4 位 = 车头左侧 4 个传感器；高 4 位 = 右侧 4 个 */
#define TASK1_CORNER_LEFT_MIN_CNT      3u         /* 左半至少亮几路才算“左有线” */
#define TASK1_CORNER_RIGHT_MAX_CNT     0u         /* 右半最多亮几路才算“右无线” */
#define TASK1_CORNER_DEBOUNCE_N        3u         /* 连续 N 次满足才触发 (10ms×3=30ms) */

/* -------- 周期常量 (需与 Task.c 中任务周期一致) -------- */
#define TASK1_CTRL_PERIOD_MS           10u

/* ========================================================================== */
/* 运行时状态                                                                 */
/* ========================================================================== */

static Task1_State_e  s_state         = TASK1_STATE_IDLE;
static uint32_t         s_state_enter_ms = 0u;      /* 本次状态进入的时间戳 */
static uint8_t        s_corner_hit_cnt = 0u;      /* 角点连续命中计数 */

static double         s_turn_angle_deg  = 0.0;    /* 本次转弯已积分的角度 (°) */
static uint32_t         s_turn_last_tick  = 0u;     /* 上次 Gz 积分的时间戳 (ms) */

static uint8_t        s_flag_count     = 0u;      /* 累计 FLAG 数 */
static uint8_t        s_lap_count      = 0u;      /* 累计完成圈数 */

/* -------- 循迹/转弯中间量 -------- */
static int16_t        s_track_error    = 0;
static uint32_t       s_track_bitmap   = 0u;
static Encoder_Snapshot s_encoder_snapshot;

/* -------- VOFA 遥测镜像 -------- */
/* ========================================================================== */
/* 静态辅助函数                                                               */
/* ========================================================================== */

static uint32_t task1_tick_ms(void)
{
    return Clock_NowMs();
}

static void task1_reset_pid_runtime(PID_T* pid)
{
    if (pid == (PID_T*)0) {
        return;
    }
    pid->target = 0.0f;
    pid->current = 0.0f;
    pid->out = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->last2_error = 0.0f;
    pid->last_out = 0.0f;
    pid->integral = 0.0f;
    pid->p_out = 0.0f;
    pid->i_out = 0.0f;
    pid->d_out = 0.0f;
    pid->last_d_out = 0.0f;
}

static void task1_brake_all(void)
{
    Motor_BrakeAll();
    task1_reset_pid_runtime(&g_tLeftMotorPID);
    task1_reset_pid_runtime(&g_tRightMotorPID);
}

static uint8_t task1_popcount8(uint32_t bits)
{
    uint8_t c = 0u;
    for (uint8_t i = 0u; i < 8u; i++) {
        if ((bits & (1u << i)) != 0u) {
            c++;
        }
    }
    return c;
}

/**
 * @brief 角点识别 (左半有线 + 右半无线)
 * @return 1=命中；0=不命中
 *
 * 角点触发条件：
 *   - 低 4 位(左半) 亮起路数 >= TASK1_CORNER_LEFT_MIN_CNT
 *   - 高 4 位(右半) 亮起路数 <= TASK1_CORNER_RIGHT_MAX_CNT
 *   - 连续 TASK1_CORNER_DEBOUNCE_N 次满足才返回 1 (去抖)
 */
static uint8_t task1_corner_hit(uint32_t bitmap)
{
    uint8_t left_cnt  = task1_popcount8(bitmap & 0x0Fu);
    uint8_t right_cnt = task1_popcount8(bitmap & 0xF0u);

    bool match = ((left_cnt  >= TASK1_CORNER_LEFT_MIN_CNT) &&
                       (right_cnt <= TASK1_CORNER_RIGHT_MAX_CNT)) ? true : false;

    if (match == true) {
        if (s_corner_hit_cnt < 0xFFu) {
            s_corner_hit_cnt++;
        }
    } else {
        s_corner_hit_cnt = 0u;
    }

    return (s_corner_hit_cnt >= TASK1_CORNER_DEBOUNCE_N) ? 1u : 0u;
}

/**
 * @brief 进入指定状态并做一次性动作
 */
static void task1_enter_state(Task1_State_e next)
{
    s_state = next;
    s_state_enter_ms = task1_tick_ms();

    switch (next) {
    case TASK1_STATE_STRAIGHT:
        s_corner_hit_cnt = 0u;
        task1_reset_pid_runtime(&g_tLeftMotorPID);
        task1_reset_pid_runtime(&g_tRightMotorPID);
        break;

    case TASK1_STATE_BEFORE_ANGLE:
        /* 继续直行，沿用速度环；不清 PID 避免抖动 */
        break;

    case TASK1_STATE_ANGLE:
        /* 进入转弯：清零积分、切为裸 PWM 驱动；先刹车一拍释放速度环输出 */
        s_turn_angle_deg = 0.0;
        s_turn_last_tick = task1_tick_ms();
        task1_brake_all();
        break;

    case TASK1_STATE_DONE:
        task1_brake_all();
        break;

    case TASK1_STATE_IDLE:
    default:
        task1_brake_all();
        break;
    }
}

/* ========================================================================== */
/* 控制子程序                                                                 */
/* ========================================================================== */

/**
 * @brief STRAIGHT / BEFORE_ANGLE 公共直行输出
 * @param use_track 1=启用循迹误差纠正；0=双轮等速
 */
static void task1_drive_straight(uint8_t use_track)
{
    float left_target  = TASK1_BASE_SPEED_MPS;
    float right_target = TASK1_BASE_SPEED_MPS;
    float left_out;
    float right_out;

    if (use_track != 0u) {
        float diff = (float)s_track_error * TASK1_TRACK_KP;
        if (diff >  TASK1_SPEED_DIFF_LIMIT_MPS) { diff =  TASK1_SPEED_DIFF_LIMIT_MPS; }
        if (diff < -TASK1_SPEED_DIFF_LIMIT_MPS) { diff = -TASK1_SPEED_DIFF_LIMIT_MPS; }

        /* error > 0 (线偏右): 车偏左 → 左轮加速、右轮减速 */
        left_target  = TASK1_BASE_SPEED_MPS + diff;
        right_target = TASK1_BASE_SPEED_MPS - diff;
    }

    pid_closeloop_motor(left_target,
                        right_target,
                        s_encoder_snapshot.speed_mps[ENCODER_LEFT],
                        s_encoder_snapshot.speed_mps[ENCODER_RIGHT],
                        &left_out,
                        &right_out);
    (void)Motor_SetOutput(MOTOR_LEFT, (int16_t)left_out);
    (void)Motor_SetOutput(MOTOR_RIGHT, (int16_t)right_out);
}

/**
 * @brief 原地左转：左轮负目标、右轮正目标，走速度环
 * @note  速度环输入单位 m/s，输出为 PWM；速度与电池电压解耦
 */
static void task1_drive_turn_left(void)
{
    float left_out;
    float right_out;

    /* 左轮倒退 -v, 右轮前进 +v；速度环会自动解析符号并输出相应 PWM */
    pid_closeloop_motor(-TASK1_TURN_SPEED_MPS,
                        +TASK1_TURN_SPEED_MPS,
                        s_encoder_snapshot.speed_mps[ENCODER_LEFT],
                        s_encoder_snapshot.speed_mps[ENCODER_RIGHT],
                        &left_out,
                        &right_out);
    (void)Motor_SetOutput(MOTOR_LEFT, (int16_t)left_out);
    (void)Motor_SetOutput(MOTOR_RIGHT, (int16_t)right_out);
}

/**
 * @brief 累积 |Gz| 到目标角度
 * @return 1=已达到 TASK1_TURN_TARGET_DEG；0=未达到
 */
static uint8_t task1_turn_integrate(void)
{
    uint32_t now = task1_tick_ms();
    double dt  = (double)(now - s_turn_last_tick) / 1000.0;
    s_turn_last_tick = now;

    /* 刚进入 ANGLE 的前 TASK1_TURN_GATE_MS 毫秒先让电机建立运动，不积分 */
    if ((now - s_state_enter_ms) < TASK1_TURN_GATE_MS) {
        return 0u;
    }

    /* 左转时 Gz 对 MPU6050 默认朝向通常为正(右手系 Z 向上)；这里直接取绝对值，
     * 避免 IMU 轴向安装方向差异带来符号困扰。精度已足够判断 90° 角。 */
    double gz = g_tMpu6050.Gz;
    s_turn_angle_deg += fabs(gz) * dt;

    return (s_turn_angle_deg >= (double)TASK1_TURN_TARGET_DEG) ? 1u : 0u;
}

/* ========================================================================== */
/* 状态机推进                                                                 */
/* ========================================================================== */

static void task1_fsm_tick(void)
{
    switch (s_state) {

    case TASK1_STATE_IDLE:
        /* 待机：保持刹车 */
        task1_brake_all();
        break;

    case TASK1_STATE_STRAIGHT:
        task1_drive_straight(1u);
        if (task1_corner_hit(s_track_bitmap) != 0u) {
            task1_enter_state(TASK1_STATE_BEFORE_ANGLE);
        }
        break;

    case TASK1_STATE_BEFORE_ANGLE:
        /* 继续直行固定时长再转弯 */
        task1_drive_straight(1u);
        if ((task1_tick_ms() - s_state_enter_ms) >= TASK1_BEFORE_ANGLE_MS) {
            task1_enter_state(TASK1_STATE_ANGLE);
        }
        break;

    case TASK1_STATE_ANGLE:
        task1_drive_turn_left();
        if (task1_turn_integrate() != 0u) {
            /* 转弯完成：+FLAG，必要时 +LAP，检查是否结束 */
            if (s_flag_count < 0xFFu) { s_flag_count++; }
            if ((s_flag_count % TASK1_FLAGS_PER_LAP) == 0u) {
                if (s_lap_count < 0xFFu) { s_lap_count++; }
            }
            if (s_lap_count >= TASK1_TARGET_LAPS) {
                task1_enter_state(TASK1_STATE_DONE);
            } else {
                task1_enter_state(TASK1_STATE_STRAIGHT);
            }
        }
        break;

    case TASK1_STATE_DONE:
    default:
        task1_brake_all();
        break;
    }
}

/* ========================================================================== */
/* VOFA 绑定                                                                  */
/* ========================================================================== */

static void task1_sync_pid_cfg(void)
{
    VofaTask1Ctx_t* ctx = VofaRegister_GetTask1Ctx();

    g_tLeftMotorPID.kp = ctx->cmd_left_kp;
    g_tLeftMotorPID.ki = ctx->cmd_left_ki;
    g_tLeftMotorPID.kd = ctx->cmd_left_kd;
    g_tRightMotorPID.kp = ctx->cmd_right_kp;
    g_tRightMotorPID.ki = ctx->cmd_right_ki;
    g_tRightMotorPID.kd = ctx->cmd_right_kd;
}

static void task1_refresh_telemetry(void)
{
    VofaTask1Ctx_t* ctx = VofaRegister_GetTask1Ctx();

    ctx->tx_state = (float)s_state;
    ctx->tx_lap = (float)s_lap_count;
    ctx->tx_flag = (float)s_flag_count;
    ctx->tx_turn_deg = (float)s_turn_angle_deg;
    ctx->tx_track_err = (float)s_track_error;
    ctx->tx_speed_l = s_encoder_snapshot.speed_mps[ENCODER_LEFT];
    ctx->tx_speed_r = s_encoder_snapshot.speed_mps[ENCODER_RIGHT];
}

/* ========================================================================== */
/* 生命周期                                                                   */
/* ========================================================================== */

void Task1_Init(void)
{
    s_state          = TASK1_STATE_IDLE;
    s_state_enter_ms = 0u;
    s_corner_hit_cnt = 0u;
    s_turn_angle_deg = 0.0;
    s_turn_last_tick = 0u;
    s_flag_count     = 0u;
    s_lap_count      = 0u;
    s_track_error    = 0;
    s_track_bitmap   = 0u;
    Encoder_GetSnapshot(&s_encoder_snapshot);
    task1_refresh_telemetry();
}

void Task1_Enter(void)
{
    s_corner_hit_cnt = 0u;
    s_turn_angle_deg = 0.0;
    s_flag_count     = 0u;
    s_lap_count      = 0u;
    s_track_error    = 0;
    s_track_bitmap   = 0u;
    Encoder_GetSnapshot(&s_encoder_snapshot);

    task1_reset_pid_runtime(&g_tLeftMotorPID);
    task1_reset_pid_runtime(&g_tRightMotorPID);
    Motor_BrakeAll();

    VofaRegister_EnterProfile(VOFA_PROFILE_TASK1);
    task1_sync_pid_cfg();
    task1_refresh_telemetry();

    task1_enter_state(TASK1_STATE_STRAIGHT);
}

void Task1_Exit(void)
{
    task1_brake_all();
    s_state = TASK1_STATE_IDLE;
    task1_refresh_telemetry();
    VofaRegister_ExitProfile();
}

/* ========================================================================== */
/* 周期任务入口                                                               */
/* ========================================================================== */

void Task1_Sample10ms(void)
{
    /* 薄封装：采样编码器速度、灰度位图、IMU 角速度 */
    /* Task_EncoderSpeedSample 已先更新唯一 Encoder 状态。 */
    Encoder_GetSnapshot(&s_encoder_snapshot);
    Track_UpdateSample();
    s_track_bitmap = Track_GetBitmap();
    s_track_error  = Calculate_Track_Error(s_track_bitmap);
    MPU6050_Read_Gyro(&g_tMpu6050);
}

void Task1_Control10ms(void)
{
    task1_sync_pid_cfg();
    task1_fsm_tick();
    Motor_Update(TASK1_CTRL_PERIOD_MS);
}

void Task1_Telemetry20ms(void)
{
    task1_refresh_telemetry();
    vofa_run();
}
