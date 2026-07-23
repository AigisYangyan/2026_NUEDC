/**
 * @file    motion.c
 * @brief   语义运动服务实现——pose + 语义目标 → 底盘速度目标 的非阻塞状态机。
 *
 * 数据链（§8.2 登记）：
 *   BoardGpio 原始计数 → Encoder_Update(chassis 拥有,10ms) → Encoder_Snapshot.total_pulses
 *     [有符号累计脉冲，前进为正] → 本服务以 last_total 差值一次性消费（非 delta_pulses 字段，
 *     那是速度环经 speed_mps 消费的口径）→ Odometry_Update → Odometry_Pose_T[x_mm,y_mm,heading_deg]
 *     → 语义误差（距离 mm / 相对角 deg）→ 控制律 → Chassis_SetTargetMps[m/s] → Chassis_Update。
 *   ImuUart FIFO → Imu_Update(本服务独占) → Imu_Snapshot[yaw_deg,valid] → 按值喂 Odometry_Update。
 *
 * 转移表：
 *   IDLE →(StartStraight,d>0)→ STRAIGHT；IDLE →(StartTurn,d≠0)→ TURN；
 *   IDLE →(StartArc,radius/角有效)→ ARC
 *   STRAIGHT →(dist≥target)→ DONE（Chassis_Stop）
 *   TURN →(|target−rel|≤tol)→ DONE（Chassis_Stop）
 *   ARC →(|arc_deg|−|rel|≤tol)→ DONE（Chassis_Stop）
 *   任意态 →(Stop)→ IDLE（Chassis_Stop）
 *   IDLE/DONE：底盘静默（不泵 Chassis_Update，刹车真值表保持）。
 *
 * 圆弧原语（S06b，计划表 §19）：双轮速度比前馈（由半径 R 与轮距 track_width 定内外轮速）
 *   + 航向误差修正（用 odometry 连续航向纠半径漂移）。轮距是本服务新增单一所有者
 *   （§19.0），仅前馈几何用，非第二航向权威；完成/修正基准均读 odometry 航向（IMU 源）。
 */
#include "app/service/motion/motion.h"

#include "app/service/chassis/chassis.h"
#include "driver/encoder/encoder.h"
#include "driver/imu/imu.h"
#include "middleware/move_profile/move_profile.h"
#include "middleware/odometry/odometry.h"
#include "middleware/pid/pid.h"

#include <math.h>
#include <stddef.h>

#define MOTION_DEG_PER_RAD 57.2957795f   /* 180/π，圆弧期望航向由弧长/半径换算用 */

/* ---- 服务私有状态（全部私有，仅经公共 API 读写） ------------------------- */

static Motion_Config_T s_cfg;
static Odometry_T      s_odo;
static Pid_T           s_hold_pid;     /* 直行航向保持外环（位置式） */

static Motion_State    s_state;
static Odometry_Pose_T s_pose;         /* 最近一次位姿（遥测/参考源） */

/* 里程计一次性消费：last_total 差值。seeded 前首拍不产生位移。 */
static int32_t s_last_total_left;
static int32_t s_last_total_right;
static bool    s_total_seeded;

/* 当前原语参考与目标。 */
static float   s_ref_x_mm;             /* STRAIGHT 起点 */
static float   s_ref_y_mm;
static float   s_ref_heading_deg;      /* STRAIGHT/TURN 航向基准 */
static bool    s_heading_hold;         /* STRAIGHT 是否启用航向保持 */
static float   s_target;               /* STRAIGHT=距离 mm；TURN/ARC=相对角/圆心角 deg */
static float   s_progress;             /* 已完成量 */

/* 圆弧原语（ARC）专属：半径 + 路径长累计 + 上拍位姿参考（弧长由 odometry 毫米位姿派生）。 */
static float   s_arc_radius_mm;        /* >0，StartArc 记录 */
static float   s_arc_len_mm;           /* 已行进弧长（∑相邻位姿弦长，非二次脉冲换算） */
static float   s_arc_prev_x_mm;        /* 上拍位姿（弧长增量基准） */
static float   s_arc_prev_y_mm;

/* PROFILED_STRAIGHT 看门狗：已运行拍数（max-runtime）、上拍已行进距离与连续无进展拍数（stall）。 */
static uint32_t s_profiled_ticks;
static float    s_profiled_last_dist_mm;
static uint32_t s_profiled_stall_ticks;
static uint32_t s_turn_ticks;           /* TURN 看门狗拍数（§37.5，StartTurn 复位） */

#define MOTION_STALL_EPS_MM 0.5f   /* 单拍进展低于此视为「无进展」（容里程量化/噪声） */

/* ---- 内部：把「一拍」里程计数据推进位姿 ---------------------------------- */

/* 读 Encoder/IMU 快照，以 total_pulses 差值一次性消费推进 odometry，并刷新 s_pose。
 * 任意态每拍调用；不推进 Encoder/IMU 状态之外的东西（Imu_Update 由调用者先行）。 */
static void motion_advance_odometry(const Imu_Snapshot_t *imu)
{
    Encoder_Snapshot enc;
    int32_t total_l;
    int32_t total_r;
    int32_t d_left;
    int32_t d_right;

    Encoder_GetSnapshot(&enc);
    total_l = enc.total_pulses[ENCODER_LEFT];
    total_r = enc.total_pulses[ENCODER_RIGHT];

    if (!s_total_seeded) {
        d_left = 0;
        d_right = 0;
        s_total_seeded = true;
    } else {
        d_left = total_l - s_last_total_left;
        d_right = total_r - s_last_total_right;
    }
    s_last_total_left = total_l;
    s_last_total_right = total_r;

    Odometry_Update(&s_odo, d_left, d_right, imu->yaw_deg, imu->valid);
    Odometry_GetPose(&s_odo, &s_pose);
}

/* ---- 生命周期 ------------------------------------------------------------- */

void Motion_Init(const Motion_Config_T *cfg)
{
    Odometry_Config_T ocfg;
    Pid_Config_T hold_cfg;

    s_cfg = (cfg != NULL) ? *cfg : (Motion_Config_T){ 0 };

    ocfg.mm_per_pulse = s_cfg.mm_per_pulse;
    ocfg.heading_sign = s_cfg.heading_sign;
    Odometry_Init(&s_odo, &ocfg);

    hold_cfg.kp = s_cfg.hold_kp;
    hold_cfg.ki = s_cfg.hold_ki;
    hold_cfg.kd = s_cfg.hold_kd;
    hold_cfg.out_limit = s_cfg.hold_diff_limit_mps;
    hold_cfg.integral_limit = 0.0f;   /* ≤0 → Pid 按 out_limit 推导 */
    hold_cfg.d_filter_alpha = 1.0f;
    Pid_Init(&s_hold_pid, &hold_cfg);

    s_state = MOTION_IDLE;
    s_pose = (Odometry_Pose_T){ 0.0f, 0.0f, 0.0f };
    s_last_total_left = 0;
    s_last_total_right = 0;
    s_total_seeded = false;
    s_ref_x_mm = 0.0f;
    s_ref_y_mm = 0.0f;
    s_ref_heading_deg = 0.0f;
    s_heading_hold = false;
    s_target = 0.0f;
    s_progress = 0.0f;
    s_arc_radius_mm = 0.0f;
    s_arc_len_mm = 0.0f;
    s_arc_prev_x_mm = 0.0f;
    s_arc_prev_y_mm = 0.0f;
    s_profiled_ticks = 0u;
    s_profiled_last_dist_mm = 0.0f;
    s_profiled_stall_ticks = 0u;
}

bool Motion_StartStraight(float distance_mm, bool heading_hold)
{
    if (distance_mm <= 0.0f) {
        return false;
    }
    s_ref_x_mm = s_pose.x_mm;
    s_ref_y_mm = s_pose.y_mm;
    s_heading_hold = heading_hold;
    s_target = distance_mm;
    s_progress = 0.0f;
    Pid_Reset(&s_hold_pid);
    s_ref_heading_deg = s_pose.heading_deg;
    s_state = MOTION_STRAIGHT;
    return true;
}

bool Motion_StartProfiledStraight(float distance_mm, bool heading_hold)
{
    if (distance_mm <= 0.0f) {
        return false;
    }
    s_ref_x_mm = s_pose.x_mm;
    s_ref_y_mm = s_pose.y_mm;
    s_heading_hold = heading_hold;
    s_target = distance_mm;
    s_progress = 0.0f;
    s_profiled_ticks = 0u;          /* 复位 max-runtime 看门狗 */
    s_profiled_last_dist_mm = 0.0f; /* 复位 stall 看门狗（起点位移=0） */
    s_profiled_stall_ticks = 0u;
    Pid_Reset(&s_hold_pid);
    s_ref_heading_deg = s_pose.heading_deg;
    s_state = MOTION_PROFILED_STRAIGHT;
    return true;
}

bool Motion_StartTurn(float relative_deg)
{
    if (relative_deg == 0.0f) {
        return false;
    }
    s_ref_heading_deg = s_pose.heading_deg;
    s_target = relative_deg;
    s_progress = 0.0f;
    s_turn_ticks = 0u;      /* §8.1 TURN 看门狗计数复位（§37.5） */
    s_state = MOTION_TURN;
    return true;
}

bool Motion_StartArc(float radius_mm, float arc_deg)
{
    if (radius_mm <= 0.0f) {
        return false;
    }
    if (arc_deg == 0.0f) {
        return false;
    }
    /* 前进圆弧约束：内轮速 = vc·(R−track/2)/R 必须 ≥0，否则内轮反向（属 TURN 语义）。 */
    if (radius_mm < s_cfg.track_width_mm * 0.5f) {
        return false;
    }
    s_arc_radius_mm = radius_mm;
    s_target = arc_deg;
    s_progress = 0.0f;
    s_arc_len_mm = 0.0f;
    s_arc_prev_x_mm = s_pose.x_mm;
    s_arc_prev_y_mm = s_pose.y_mm;
    s_ref_heading_deg = s_pose.heading_deg;
    Pid_Reset(&s_hold_pid);   /* 复用航向保持 PID 实例；单活动原语保证不与直行纠偏并发 */
    s_state = MOTION_ARC;
    return true;
}

/* ---- 控制律 --------------------------------------------------------------- */

/* STRAIGHT：Euclidean 位移判到位；航向保持外环产生差速修正。
 * 差速符号约定：rel>0（航向较起点偏 CCW/左）→ 需右转（CW）→ 左快右慢。
 *   corr = Pid_UpdatePositional(0, rel) ≈ −kp·rel（rel>0 → corr<0）
 *   left = base − corr（rel>0 → 变大）、right = base + corr（rel>0 → 变小）→ 左快右慢 CW。 */
static void motion_step_straight(bool heading_valid)
{
    float dx = s_pose.x_mm - s_ref_x_mm;
    float dy = s_pose.y_mm - s_ref_y_mm;
    float dist = sqrtf(dx * dx + dy * dy);
    float base = s_cfg.straight_speed_mps;
    float corr = 0.0f;

    s_progress = dist;

    if (dist >= s_target) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;   /* DONE：本拍不泵内环，刹车保持 */
    }

    if (s_heading_hold && heading_valid) {
        float rel = s_pose.heading_deg - s_ref_heading_deg;
        corr = Pid_UpdatePositional(&s_hold_pid, 0.0f, rel);
    }
    Chassis_SetTargetMps(base - corr, base + corr);
    Chassis_Update();
}

/* PROFILED_STRAIGHT：梯形剖面定长直行。纵向前馈由 move_profile 按已行进距离产出
 * （前馈 = 位置闭环，无纵向 PID）；横向沿用航向保持外环（与 motion_step_straight 同实例、同符号约定）。
 *   base = MoveProfile_Speed(剖面 cfg, dist, target)（自限 [0,cruise]，永不超匀速上限）；
 *   dist>=target → Chassis_Stop + DONE（与恒速直行同一到位判据）。
 * 单一所有者：距离→前馈速度归 move_profile；dist（起点欧氏位移）沿用直行同一算法（motion.c 既有所有者，
 *   非第二距离源）；差速限幅/换向/超时/刹车归 motor.c 经 chassis、航向差速限幅归 hold pid cfg——本函数不复做。 */
static void motion_step_profiled_straight(bool heading_valid)
{
    float dx = s_pose.x_mm - s_ref_x_mm;
    float dy = s_pose.y_mm - s_ref_y_mm;
    float dist = sqrtf(dx * dx + dy * dy);
    float delta = dist - s_profiled_last_dist_mm;   /* 本拍行进增量（stall 判据） */
    MoveProfile_Config_T pcfg;
    float base;
    float corr = 0.0f;

    s_progress = dist;
    s_profiled_last_dist_mm = dist;
    s_profiled_ticks++;

    if (dist >= s_target) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;   /* DONE：本拍不泵内环，刹车保持 */
    }

    pcfg.cruise_mps = s_cfg.profile_cruise_mps;
    pcfg.start_mps = s_cfg.profile_start_mps;
    pcfg.accel_mps2 = s_cfg.profile_accel_mps2;
    pcfg.decel_mps2 = s_cfg.profile_decel_mps2;
    base = MoveProfile_Speed(&pcfg, dist, s_target);

    /* §8.1 双看门狗（完成判据优先；所有者=motion，均→Chassis_Stop+DONE，0=禁用）。 */
    /* ① 堵转看门狗（快，保护 TB6612）：命令在动(base>0)但里程无进展连续 N 拍→切停。
     *    防起步速偏低卡住/编码器脱线/撞障时，增量式速度环积分顶满 PWM 灌堵转电流。 */
    if (base > 0.0f && delta < MOTION_STALL_EPS_MM) {
        s_profiled_stall_ticks++;
    } else {
        s_profiled_stall_ticks = 0u;
    }
    if ((s_cfg.profile_stall_ticks > 0u) &&
        (s_profiled_stall_ticks >= s_cfg.profile_stall_ticks)) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;
    }
    /* ② 运行拍数上限（慢兜底）：移动但永不达标（如 mm_per_pulse 严重失标致物理过冲）→ 切停。 */
    if ((s_cfg.profile_timeout_ticks > 0u) &&
        (s_profiled_ticks >= s_cfg.profile_timeout_ticks)) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;
    }

    if (s_heading_hold && heading_valid) {
        float rel = s_pose.heading_deg - s_ref_heading_deg;
        corr = Pid_UpdatePositional(&s_hold_pid, 0.0f, rel);
    }
    Chassis_SetTargetMps(base - corr, base + corr);
    Chassis_Update();
}

/* TURN：odometry 连续航向闭环。目标相对角 s_target（+ = CCW）。
 *   rel = heading − 基准；err = target − rel；|err|≤tol → 到位停。
 *   cmd = clamp(turn_kp·err, ±turn_speed)；left = −cmd、right = +cmd（cmd>0 → CCW）。 */
static void motion_step_turn(void)
{
    float rel = s_pose.heading_deg - s_ref_heading_deg;
    float err = s_target - rel;
    float lim = s_cfg.turn_speed_mps;
    float cmd;

    s_progress = rel;
    s_turn_ticks++;

    if (fabsf(err) <= s_cfg.turn_tol_deg) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;
    }

    /* §8.1 TURN 看门狗（§37.5，profiled 同款骨架，0=禁用）：拍数超限→切停。
     * 覆盖两个无限差速场景：IMU 断线航向冻结（err 恒定）、近容差物理失速。 */
    if ((s_cfg.turn_timeout_ticks > 0u) && (s_turn_ticks >= s_cfg.turn_timeout_ticks)) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;
    }

    /* 比例律：cmd 随剩余角收敛。已知裕度（arch-auditor 建议级，§15.5）——接近容差时
     * cmd→turn_kp·turn_tol_deg；若低于电机实测启动速度会在到位前物理失速，使 DONE 不触发。
     * 标定约束：turn_kp·turn_tol_deg 须高于实测启动速度，或由调用者(T01)对转向设完成超时。
     * 不在此加最小驱动下限：其值只能实测标定、主机 fake 无静摩擦无法验证（§8.1/§8.3）。 */
    cmd = s_cfg.turn_kp * err;
    if (cmd > lim) {
        cmd = lim;
    } else if (cmd < -lim) {
        cmd = -lim;
    }
    Chassis_SetTargetMps(-cmd, cmd);
    Chassis_Update();
}

/* ARC：定半径圆弧。前馈内外轮速比 + odometry 连续航向误差修正。
 *   完成判据（航向驱动，IMU 权威）：|arc_deg|−|rel|≤turn_tol_deg。
 *   前馈：half=track/2；v_inner=vc·(R−half)/R、v_outer=vc·(R+half)/R；
 *         CCW(dir>0) 左内右外、CW 左外右内。
 *   修正：期望已转角 exp=dir·deg(arc_len/R)（幅值夹至 |arc_deg|）；
 *         corr=Pid(exp, rel)；left−=corr、right+=corr（欠转→推向转更多，CCW/CW 经号自洽）。
 * 单一所有者：脉冲→距离/yaw 符号/unwrap/限幅/刹车均归既有所有者，本函数只做圆弧几何与修正参考。 */
static void motion_step_arc(void)
{
    float rel = s_pose.heading_deg - s_ref_heading_deg;
    float dx = s_pose.x_mm - s_arc_prev_x_mm;
    float dy = s_pose.y_mm - s_arc_prev_y_mm;
    float dir = (s_target >= 0.0f) ? 1.0f : -1.0f;
    float radius = s_arc_radius_mm;
    float half = s_cfg.track_width_mm * 0.5f;
    float vc = s_cfg.arc_speed_mps;
    float v_inner = vc * (radius - half) / radius;
    float v_outer = vc * (radius + half) / radius;
    float left;
    float right;
    float exp_deg;
    float corr;

    /* 弧长累计：消费 odometry 毫米位姿的相邻弦长（非第二次脉冲→距离换算）。 */
    s_arc_len_mm += sqrtf(dx * dx + dy * dy);
    s_arc_prev_x_mm = s_pose.x_mm;
    s_arc_prev_y_mm = s_pose.y_mm;
    s_progress = rel;

    if ((fabsf(s_target) - fabsf(rel)) <= s_cfg.turn_tol_deg) {
        Chassis_Stop();
        s_state = MOTION_DONE;
        return;   /* DONE：本拍不泵内环，刹车保持 */
    }

    if (dir > 0.0f) {          /* CCW：左内右外 */
        left = v_inner;
        right = v_outer;
    } else {                   /* CW：左外右内 */
        left = v_outer;
        right = v_inner;
    }

    exp_deg = dir * (s_arc_len_mm / radius) * MOTION_DEG_PER_RAD;
    if (fabsf(exp_deg) > fabsf(s_target)) {
        exp_deg = s_target;    /* 夹到目标（同号，幅值 = |arc_deg|） */
    }
    corr = Pid_UpdatePositional(&s_hold_pid, exp_deg, rel);
    left -= corr;
    right += corr;

    Chassis_SetTargetMps(left, right);
    Chassis_Update();
}

void Motion_Update(void)
{
    Imu_Snapshot_t imu;

    /* ① 本服务独占：排空 IMU FIFO 并刷新快照。 */
    Imu_Update();
    Imu_GetSnapshot(&imu);

    /* ②③④ 一次性消费里程计并取位姿（任意态，用于遥测与参考捕获）。 */
    motion_advance_odometry(&imu);

    /* ⑤⑥ 依状态推进控制律；IDLE/DONE 底盘静默。 */
    switch (s_state) {
    case MOTION_STRAIGHT:
        motion_step_straight(imu.valid);
        break;
    case MOTION_PROFILED_STRAIGHT:
        motion_step_profiled_straight(imu.valid);
        break;
    case MOTION_TURN:
        motion_step_turn();
        break;
    case MOTION_ARC:
        motion_step_arc();
        break;
    case MOTION_IDLE:
    case MOTION_DONE:
    default:
        /* 静默：不设目标、不泵 Chassis_Update，刹车真值表保持。 */
        break;
    }
}

void Motion_Stop(void)
{
    Chassis_Stop();
    s_state = MOTION_IDLE;
    s_progress = 0.0f;
}

Motion_State Motion_GetState(void)
{
    return s_state;
}

bool Motion_IsDone(void)
{
    return (s_state == MOTION_DONE);
}

void Motion_GetTelemetry(Motion_Telemetry_T *out)
{
    bool active;

    if (out == NULL) {
        return;
    }
    active = (s_state == MOTION_STRAIGHT) || (s_state == MOTION_TURN) ||
             (s_state == MOTION_ARC) || (s_state == MOTION_PROFILED_STRAIGHT);

    out->state = s_state;
    out->x_mm = s_pose.x_mm;
    out->y_mm = s_pose.y_mm;
    out->heading_deg = s_pose.heading_deg;
    out->target = active ? s_target : 0.0f;
    out->progress = active ? s_progress : 0.0f;
}

void Motion_SetProfileParams(float cruise_mps, float start_mps,
                             float accel_mps2, float decel_mps2)
{
    /* 额外写路径：更新已应用剖面参数（同一所有者 s_cfg，非新所有者）。
     * 不发电机命令、不改状态机——下一/当前 PROFILED_STRAIGHT 步进读用即时生效。 */
    s_cfg.profile_cruise_mps = cruise_mps;
    s_cfg.profile_start_mps = start_mps;
    s_cfg.profile_accel_mps2 = accel_mps2;
    s_cfg.profile_decel_mps2 = decel_mps2;
}

void Motion_GetProfileParams(float *cruise_mps, float *start_mps,
                             float *accel_mps2, float *decel_mps2)
{
    if ((cruise_mps == NULL) || (start_mps == NULL) ||
        (accel_mps2 == NULL) || (decel_mps2 == NULL)) {
        return;   /* 任一 NULL 视为误用，无副作用（同 GetTelemetry 口径） */
    }
    *cruise_mps = s_cfg.profile_cruise_mps;
    *start_mps = s_cfg.profile_start_mps;
    *accel_mps2 = s_cfg.profile_accel_mps2;
    *decel_mps2 = s_cfg.profile_decel_mps2;
}

void Motion_SetHeadingTuning(float hold_kp, float hold_ki, float hold_kd, float turn_kp)
{
    /* 额外写路径（同一所有者 s_cfg）：turn_kp 每拍活读即时生效；hold 三增益须同步
     * 在线 PID（§37.5 修订 2：Start* 只 Pid_Reset 不重灌 cfg）——双写后全部即时生效。 */
    s_cfg.hold_kp = hold_kp;
    s_cfg.hold_ki = hold_ki;
    s_cfg.hold_kd = hold_kd;
    s_cfg.turn_kp = turn_kp;
    Pid_SetGains(&s_hold_pid, hold_kp, hold_ki, hold_kd);
}

void Motion_GetHeadingTuning(float *hold_kp, float *hold_ki, float *hold_kd, float *turn_kp)
{
    if ((hold_kp == NULL) || (hold_ki == NULL) || (hold_kd == NULL) || (turn_kp == NULL)) {
        return;   /* 同 GetProfileParams 口径 */
    }
    *hold_kp = s_cfg.hold_kp;
    *hold_ki = s_cfg.hold_ki;
    *hold_kd = s_cfg.hold_kd;
    *turn_kp = s_cfg.turn_kp;
}
