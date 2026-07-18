/**
 * @file    route.h
 * @brief   分段路线执行服务（App Service 层）——「段序编排 + 段完成触发 + 段间交接」唯一所有者。
 *
 * 抽象（底盘能做什么）：
 * - 按一张装配层注入的段表，依次执行四类运动基元——沿线走到某个循迹元素、走一段直线、
 *   原地转一个角度、走一段定半径圆弧；每段自然完成自动切下一段；
 * - 全程可随时确定性停车；报告当前执行到第几段与总体状态。
 * 仅此成为公共面。
 *
 * 隐藏：
 * - 用了哪些下层 Service（line_follow / motion）、段状态机内部布局、段级超时门控、
 *   交接刹停间隙与 odometry catch-up 细节。
 *
 * 分层与所有权（AGENTS.md §3.4 / §8.1 / §8.2；phase4 计划表 §20）：
 * - route 只 #include line_follow.h 与 motion.h（Service→Service 同层受控）；绝不触碰
 *   scheduler.h、track_elements.h、driver 与 middleware 各层、chassis.h。
 * - route 新增唯一拥有：段序状态机 + 段完成分派（元素事件 / Motion_IsDone）+ 段间确定性交接
 *   （隔拍刹停间隙 + 进 motion 段前 odometry catch-up）+ 段级完成超时兜底。
 * - route 对任何数据变换零复做——循迹外环/丢线/速度调制/元素几何归 line_follow 子链；
 *   语义运动/到位判据/航向/圆弧前馈/IMU 排空/里程计归 motion 子链；限幅/slew/换向/超时/刹车
 *   归 motor.c 经 chassis；元素位序 bit0_is_left 归 line_follow 配置。
 *
 * 每拍至多推进一个子服务：Route_Update 在 RUNNING 期一拍只推进当前段的唯一子服务
 * （FOLLOW_UNTIL→LineFollow_Update / motion 段→Motion_Update），两子服务永不同拍并发驱动底盘，
 * 故 route 不构成第四个 Chassis_Update 推进点、不构成第二个 Imu_Update 排空点。
 *
 * 钩子签名兼容：Route_Start(void)/Route_Update(uint32_t)/Route_Stop(void) 恰配 Scheduler_Entry_T
 * 的 on_enter/on_step/on_exit，T01 可直接把 route 注册为运行条目，无需适配器。
 *
 * 调用前置条件：装配层已 LineFollow_Init + Motion_Init（各带有效标定 cfg），且底盘链已初始化。
 */
#ifndef HC_TEAM_APP_SERVICE_ROUTE_ROUTE_H
#define HC_TEAM_APP_SERVICE_ROUTE_ROUTE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 段类型：单张段表可混合排列的四类运动基元。 */
typedef enum {
    ROUTE_SEG_FOLLOW_UNTIL = 0, /* 循迹直到命中指定元素事件（line_follow 驱动） */
    ROUTE_SEG_STRAIGHT,         /* 直行定距（motion 驱动） */
    ROUTE_SEG_TURN,             /* 原地定角转（motion 驱动） */
    ROUTE_SEG_ARC,              /* 定半径定角圆弧（motion 驱动） */
} Route_SegKind;

/** 段描述符（装配层填写；kind 决定哪些字段有效，其余忽略）。 */
typedef struct {
    Route_SegKind kind;
    /* FOLLOW_UNTIL */
    uint16_t until_elements;    /* LineFollow_Element 位掩码，任一位命中(OR)即段完成；
                                   ==0 = 无元素目标（该段仅靠 timeout/LOST/Route_Stop 终止，谨慎） */
    /* STRAIGHT */
    float    distance_mm;       /* 前进距离（>0，透传 Motion_StartStraight） */
    bool     heading_hold;      /* 直行航向保持开关（透传 Motion_StartStraight） */
    /* TURN */
    float    turn_deg;          /* 相对角（≠0，+CCW/−CW，透传 Motion_StartTurn） */
    /* ARC */
    float    arc_radius_mm;     /* 半径（>track_width/2，透传 Motion_StartArc） */
    float    arc_deg;           /* 圆心角（≠0，+CCW/−CW，透传 Motion_StartArc） */
    /* 安全：段完成超时兜底（route 唯一拥有的段级 liveness 保护） */
    uint32_t timeout_ms;        /* >0 = 该段自进入起超过此时长仍未完成 → FAULT + 确定性停车；
                                   ==0 = 禁用段级超时（该段仅靠自然完成 / line_follow LOST /
                                   Route_Stop 终止；motion 段若失速则可能永不完成——见 .c 安全注释） */
} Route_Segment_T;

/** 服务状态。 */
typedef enum {
    ROUTE_IDLE = 0,   /* 未运行 / 已停：route 不推任何子服务，底盘静默（刹车真值表保持） */
    ROUTE_RUNNING,    /* 执行某段中 */
    ROUTE_DONE,       /* 全部段完成：末段已确定性停车，静默 */
    ROUTE_FAULT,      /* 段失败（line_follow LOST / 段超时 / 段启动被拒）：已确定性停车，静默 */
} Route_State;

/** 一次性读出的服务遥测。 */
typedef struct {
    Route_State   state;
    uint8_t       segment_index;  /* 当前（RUNNING）或最后处理（DONE/FAULT）的段索引 */
    uint8_t       segment_count;  /* 段表长度 */
    Route_SegKind current_kind;   /* segment_index 段的类型（count==0 时无意义） */
} Route_Telemetry_T;

/**
 * @brief  登记段表 + 复位为 IDLE。
 * @param  segments  段表首址（调用方保证表生命周期覆盖使用期）。
 * @param  count     段数；segments==NULL 或 count==0 → 合法空表（Route_Start 立即 DONE）。
 * @note   不触碰任何硬件/子服务。前置：装配层已 LineFollow_Init + Motion_Init（各带有效标定 cfg）。
 */
void Route_Setup(const Route_Segment_T *segments, uint8_t count);

/**
 * @brief  开始执行（匹配 scheduler on_enter 签名）。
 * @note   空表 → state=DONE（trivially 完成）。非空 → cur=0、标记首段待进入、state=RUNNING；
 *         本调用不驱动底盘（驱动始于首个 Route_Update）。
 */
void Route_Start(void);

/**
 * @brief  推进一拍（匹配 scheduler on_step 签名）。
 * @param  now_ms  单调毫秒时钟（段级超时基准，由装配层主循环注入）。
 * @note   RUNNING 期每拍至多推进一个子服务；IDLE/DONE/FAULT 完全静默（不推任何子服务，刹车保持）。
 */
void Route_Update(uint32_t now_ms);

/**
 * @brief  确定性停止（匹配 scheduler on_exit 签名）。
 * @note   停当前活动子服务（FOLLOW_UNTIL:LineFollow_Stop / motion 段:Motion_Stop）→ state=IDLE。
 *         随时可从正常控制流调用（§8.1）。
 */
void Route_Stop(void);

/** @brief 当前状态。 */
Route_State Route_GetState(void);

/** @brief 是否全部段完成（state==ROUTE_DONE）。 */
bool Route_IsDone(void);

/** @brief 复制遥测快照。out==NULL 无副作用。 */
void Route_GetTelemetry(Route_Telemetry_T *out);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_APP_SERVICE_ROUTE_ROUTE_H */
