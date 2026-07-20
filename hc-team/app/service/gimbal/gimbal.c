/**
 * @file    gimbal.c
 * @brief   云台视觉瞄准服务实现（契约 §21.3）
 *
 * 状态机（转移表）：
 *   IDLE       --SelectTopic ok-->        HANDSHAKING
 *   HANDSHAKING--确认帧号匹配-->           ARMING(cur_pulse=0)
 *   HANDSHAKING--ack_timeout_ms-->         STOPPED
 *   ARMING     --enable×2+preset×2+clear×2 发完--> AIMING
 *   AIMING     --coord seq 停顿≥timeout--> STOPPED（短暂停顿保持 AIMING 静默不动）
 *   任意态      --Gimbal_Stop-->            STOPPED
 *   任意态      --SelectTopic-->            HANDSHAKING（重选题）
 */
#include "app/service/gimbal/gimbal.h"

#include "app/service/gimbal/gimbal_stepbus.h"
#include "driver/clock/clock.h"
#include "driver/uart_vision/uart_vision.h"

#include <stddef.h>

/* ARMING 期逐拍下发的六帧序：enable X→Y（使能锁轴）→ preset X→Y（F1 设 mode=ABSOLUTE+速度）
 * → clear X→Y（0x0A 建立绝对坐标零点，令 cur_pulse=0 对应装配时物理位置）。 */
#define GIMBAL_ARM_STEP_ENABLE_X 0u
#define GIMBAL_ARM_STEP_ENABLE_Y 1u
#define GIMBAL_ARM_STEP_PRESET_X 2u
#define GIMBAL_ARM_STEP_PRESET_Y 3u
#define GIMBAL_ARM_STEP_CLEAR_X  4u
#define GIMBAL_ARM_STEP_CLEAR_Y  5u
#define GIMBAL_ARM_STEP_DONE     6u

static Gimbal_Config_T s_cfg;
static bool            s_has_cfg = false;
static Gimbal_State    s_state = GIMBAL_STATE_IDLE;

static int32_t  s_cur_pulse[VISION_AIM_AXIS_COUNT];
static float    s_prev_error_px[VISION_AIM_AXIS_COUNT];  /* D 项状态：上一拍误差，逐拍喂 VisionAim_Map（比照 cur_pulse 调用方持状态先例） */
static bool     s_aim_prev_seeded = false;               /* 进 AIMING 后首帧是否已播种 prev（令首拍 de=0，无首拍 D 冲击） */

/* 自门控 */
static uint32_t s_gate_base_ms = 0u;
static bool     s_has_gate_base = false;

/* 握手 */
static uint8_t  s_pending_main = 0u;
static uint8_t  s_pending_sub = 0u;
static uint32_t s_ack_seq_base = 0u;
static uint32_t s_handshake_start_ms = 0u;

/* ARMING */
static uint8_t  s_arm_step = GIMBAL_ARM_STEP_DONE;
static uint32_t s_arm_start_ms = 0u;   /* ARMING 起始时刻，setup 期总线活性上限（复用 ack_timeout_ms） */

/* AIMING 时效 */
static bool     s_has_coord_seq = false;
static uint32_t s_last_coord_seq = 0u;
static uint32_t s_last_fresh_ms = 0u;

/* 遥测缓存 */
static float    s_last_coord_x = 0.0f;
static float    s_last_coord_y = 0.0f;
static bool     s_axis_active[VISION_AIM_AXIS_COUNT];
static uint8_t  s_ack_main = 0u;
static uint8_t  s_ack_sub = 0u;

static void gimbal_reset_runtime(void)
{
    s_state = GIMBAL_STATE_IDLE;
    s_cur_pulse[VISION_AIM_AXIS_X] = 0;
    s_cur_pulse[VISION_AIM_AXIS_Y] = 0;
    s_prev_error_px[VISION_AIM_AXIS_X] = 0.0f;
    s_prev_error_px[VISION_AIM_AXIS_Y] = 0.0f;
    s_aim_prev_seeded = false;
    s_has_gate_base = false;
    s_gate_base_ms = 0u;
    s_pending_main = 0u;
    s_pending_sub = 0u;
    s_ack_seq_base = 0u;
    s_handshake_start_ms = 0u;
    s_arm_step = GIMBAL_ARM_STEP_DONE;
    s_arm_start_ms = 0u;
    s_has_coord_seq = false;
    s_last_coord_seq = 0u;
    s_last_fresh_ms = 0u;
    s_last_coord_x = 0.0f;
    s_last_coord_y = 0.0f;
    s_axis_active[VISION_AIM_AXIS_X] = false;
    s_axis_active[VISION_AIM_AXIS_Y] = false;
    s_ack_main = 0u;
    s_ack_sub = 0u;
}

void Gimbal_Init(const Gimbal_Config_T *config)
{
    if (config == NULL) {
        return;   /* 误用：不写（同 pid/odometry 口径） */
    }

    s_cfg = *config;
    s_has_cfg = true;
    VisionAim_Init(&s_cfg.aim);
    GimbalStepbus_Init();
    gimbal_reset_runtime();
    /* 安全起点：不发选题、不发移动、不 enable。 */
}

bool Gimbal_SelectTopic(uint8_t main_task, uint8_t sub_task)
{
    if (s_has_cfg == false) {
        return false;
    }

    if (UartVision_SendTopic(main_task, sub_task) == false) {
        return false;   /* TX 忙：保持原态 */
    }

    s_pending_main = main_task;
    s_pending_sub = sub_task;
    s_ack_seq_base = UartVision_GetTopicAckSeq();
    s_handshake_start_ms = Clock_NowMs();
    s_has_gate_base = false;   /* 立即让下一拍进状态机（不被上一次门控挡住） */
    s_state = GIMBAL_STATE_HANDSHAKING;
    return true;
}

static void gimbal_step_handshaking(uint32_t now_ms)
{
    uint32_t ack_seq = UartVision_GetTopicAckSeq();

    if (ack_seq != s_ack_seq_base) {
        uint8_t got_main = 0u;
        uint8_t got_sub = 0u;

        s_ack_seq_base = ack_seq;   /* 只对新确认帧反应一次 */
        if ((UartVision_GetTopicAck(&got_main, &got_sub) == true) &&
            (got_main == s_pending_main) && (got_sub == s_pending_sub)) {
            s_ack_main = got_main;
            s_ack_sub = got_sub;
            s_cur_pulse[VISION_AIM_AXIS_X] = 0;
            s_cur_pulse[VISION_AIM_AXIS_Y] = 0;
            s_arm_step = GIMBAL_ARM_STEP_ENABLE_X;
            s_arm_start_ms = now_ms;
            s_state = GIMBAL_STATE_ARMING;
            return;
        }
        /* 回显号不匹配：忽略，继续等（防视觉解析失败而盲跑） */
    }

    if ((uint32_t)(now_ms - s_handshake_start_ms) >= s_cfg.ack_timeout_ms) {
        s_state = GIMBAL_STATE_STOPPED;
    }
}

static void gimbal_step_arming(uint32_t now_ms)
{
    bool sent = false;

    /* setup 期总线活性上限（§8.1 命令过期即停，与 HANDSHAKING/AIMING 同口径）：
     * 步进总线故障使 IsIdle 永不为真时，ARMING 不得永久滞留在已使能态。复用 ack_timeout_ms。 */
    if ((uint32_t)(now_ms - s_arm_start_ms) >= s_cfg.ack_timeout_ms) {
        s_state = GIMBAL_STATE_STOPPED;
        return;
    }

    if (GimbalStepbus_IsIdle() == false) {
        return;   /* 总线忙：下一拍再发一帧 */
    }

    switch (s_arm_step) {
        case GIMBAL_ARM_STEP_ENABLE_X:
            sent = GimbalStepbus_TrySendEnable(GIMBAL_STEPBUS_AXIS_X, true);
            break;
        case GIMBAL_ARM_STEP_ENABLE_Y:
            sent = GimbalStepbus_TrySendEnable(GIMBAL_STEPBUS_AXIS_Y, true);
            break;
        case GIMBAL_ARM_STEP_PRESET_X:
            sent = GimbalStepbus_TrySendPreset(GIMBAL_STEPBUS_AXIS_X, s_cfg.step_speed_rpm);
            break;
        case GIMBAL_ARM_STEP_PRESET_Y:
            sent = GimbalStepbus_TrySendPreset(GIMBAL_STEPBUS_AXIS_Y, s_cfg.step_speed_rpm);
            break;
        case GIMBAL_ARM_STEP_CLEAR_X:
            sent = GimbalStepbus_TrySendClearZero(GIMBAL_STEPBUS_AXIS_X);
            break;
        case GIMBAL_ARM_STEP_CLEAR_Y:
            sent = GimbalStepbus_TrySendClearZero(GIMBAL_STEPBUS_AXIS_Y);
            break;
        default:
            break;
    }

    if (sent == true) {
        s_arm_step++;
        if (s_arm_step >= GIMBAL_ARM_STEP_DONE) {
            s_has_coord_seq = false;
            s_last_fresh_ms = Clock_NowMs();
            s_aim_prev_seeded = false;   /* 进 AIMING：首帧重新播种 prev */
            s_state = GIMBAL_STATE_AIMING;
        }
    }
}

/* AIMING 一拍：把一帧新坐标映射为脉冲增量，累加进双轴绝对目标，并一帧（0xAA）发出双轴绝对目标。
 * 绝对方案：累加无条件（忙帧目标仍推进，下一拍发最新绝对目标自愈，不重放中间增量）；
 * 轴程限幅由 vision_aim 依传入的 cur_pulse 施加，本服务不复算。 */
static void gimbal_aim_dispatch(void)
{
    UartVision_Coord_T coord;
    VisionAim_Result_T res;

    if (UartVision_GetLatestCoord(&coord) == false) {
        return;
    }

    s_last_coord_x = coord.x;
    s_last_coord_y = coord.y;

    VisionAim_Map(coord.x, coord.y,
                  s_cur_pulse[VISION_AIM_AXIS_X], s_cur_pulse[VISION_AIM_AXIS_Y],
                  s_prev_error_px[VISION_AIM_AXIS_X], s_prev_error_px[VISION_AIM_AXIS_Y],
                  &res);

    if (s_aim_prev_seeded == false) {
        /* 首帧：以本帧误差播种 prev，令 de=0（无首拍 D 冲击），据此重算纯 P 增量后再下发。 */
        s_prev_error_px[VISION_AIM_AXIS_X] = res.error_px[VISION_AIM_AXIS_X];
        s_prev_error_px[VISION_AIM_AXIS_Y] = res.error_px[VISION_AIM_AXIS_Y];
        s_aim_prev_seeded = true;
        VisionAim_Map(coord.x, coord.y,
                      s_cur_pulse[VISION_AIM_AXIS_X], s_cur_pulse[VISION_AIM_AXIS_Y],
                      s_prev_error_px[VISION_AIM_AXIS_X], s_prev_error_px[VISION_AIM_AXIS_Y],
                      &res);
    }

    /* 存本拍误差供下一拍 de（死区拍也存，error_px 恒写出）。 */
    s_prev_error_px[VISION_AIM_AXIS_X] = res.error_px[VISION_AIM_AXIS_X];
    s_prev_error_px[VISION_AIM_AXIS_Y] = res.error_px[VISION_AIM_AXIS_Y];

    s_axis_active[VISION_AIM_AXIS_X] = res.active[VISION_AIM_AXIS_X];
    s_axis_active[VISION_AIM_AXIS_Y] = res.active[VISION_AIM_AXIS_Y];

    /* 唯一累加点：无条件推进双轴绝对目标（delta 已含轴程限幅；死区拍 delta=0 即目标不变）。 */
    s_cur_pulse[VISION_AIM_AXIS_X] += res.delta_pulse[VISION_AIM_AXIS_X];
    s_cur_pulse[VISION_AIM_AXIS_Y] += res.delta_pulse[VISION_AIM_AXIS_Y];

    /* 总线空 → 一帧发双轴绝对目标（忙则本拍不发，下一拍发最新目标自愈）。 */
    (void)GimbalStepbus_TrySendDualAbsolute(s_cur_pulse[VISION_AIM_AXIS_X],
                                            s_cur_pulse[VISION_AIM_AXIS_Y]);
}

static void gimbal_step_aiming(uint32_t now_ms)
{
    uint32_t coord_seq = UartVision_GetCoordSeq();

    if (s_has_coord_seq == false) {
        s_has_coord_seq = true;
        s_last_coord_seq = coord_seq;
        s_last_fresh_ms = now_ms;
        return;
    }

    if (coord_seq != s_last_coord_seq) {
        s_last_coord_seq = coord_seq;
        s_last_fresh_ms = now_ms;
        gimbal_aim_dispatch();
        return;
    }

    /* seq 停顿：短暂停顿保持 AIMING 静默不动；达超时 → 安全停。 */
    if ((uint32_t)(now_ms - s_last_fresh_ms) >= s_cfg.coord_timeout_ms) {
        s_state = GIMBAL_STATE_STOPPED;
    }
}

void Gimbal_Update(void)
{
    uint32_t now_ms = 0u;

    /* 末尾恒推进步进总线服务（消费 TX 完成 + drain 步进 RX），不受门控约束。 */
    GimbalStepbus_Service();

    if (s_has_cfg == false) {
        return;
    }

    now_ms = Clock_NowMs();
    if ((s_has_gate_base == true) &&
        ((uint32_t)(now_ms - s_gate_base_ms) < GIMBAL_UPDATE_PERIOD_MS)) {
        return;   /* 未到周期 */
    }
    s_gate_base_ms = now_ms;
    s_has_gate_base = true;

    UartVision_Poll();   /* drain 视觉帧 → 刷新坐标/确认缓存 */

    switch (s_state) {
        case GIMBAL_STATE_HANDSHAKING:
            gimbal_step_handshaking(now_ms);
            break;
        case GIMBAL_STATE_ARMING:
            gimbal_step_arming(now_ms);
            break;
        case GIMBAL_STATE_AIMING:
            gimbal_step_aiming(now_ms);
            break;
        case GIMBAL_STATE_IDLE:
        case GIMBAL_STATE_STOPPED:
        default:
            break;   /* 静默：不下发 */
    }
}

void Gimbal_Stop(void)
{
    /* 确定性安全停：停止下发（STOPPED 态 Update 不下发）。步进保持使能（保持位置力矩）。
     * gimbal_stepbus 无待发队列（发送即时），无需清；cur_pulse 位置保留。 */
    s_state = GIMBAL_STATE_STOPPED;
}

Gimbal_State Gimbal_GetState(void)
{
    return s_state;
}

void Gimbal_GetTelemetry(Gimbal_Telemetry_T *out)
{
    if (out == NULL) {
        return;
    }

    out->state = s_state;
    out->cur_pulse[VISION_AIM_AXIS_X] = s_cur_pulse[VISION_AIM_AXIS_X];
    out->cur_pulse[VISION_AIM_AXIS_Y] = s_cur_pulse[VISION_AIM_AXIS_Y];
    out->last_coord_x = s_last_coord_x;
    out->last_coord_y = s_last_coord_y;
    out->last_coord_seq = s_last_coord_seq;
    out->axis_active[VISION_AIM_AXIS_X] = s_axis_active[VISION_AIM_AXIS_X];
    out->axis_active[VISION_AIM_AXIS_Y] = s_axis_active[VISION_AIM_AXIS_Y];
    out->ack_main = s_ack_main;
    out->ack_sub = s_ack_sub;
}
