/**
 * @file    track_elements.h
 * @brief   循迹元素检测器（Middleware）——深色位图流的几何类别识别 + 连续置信去毛刺
 *
 * 模块职责：
 * - 唯一能力：把逐拍传入的 12 路深色位图，判定为一类循迹路面**几何元素**
 *   （断线 / 横线 / 左岔 / 右岔），经连续置信计数去毛刺后，输出确认的上升沿事件。
 *
 * 设计约定（AGENTS.md §3.3 / §8.2 / phase4 计划表 §16 契约）：
 * 1. 纯算法、状态由调用者显式持有（TrackElements_Detector_T 上下文，无 malloc、无模块 static）。
 *    不读传感器：位图由调用者**按值**传入（Middleware 不含 Driver 头）。M02 不采样——
 *    Gray_ReadDarkBitmap() 的唯一触发所有者仍是 LineFollow_Update()（10ms 门控），
 *    绝不新开第二个采样点（否则同拓扑 V21 双泵）。
 * 2. 只判**几何类别**，不做赛道**语义**裁定：FULL_BAR 不区分「十字」与「终点横线」，
 *    「第几次横线 / 是否终点」归段路线执行（S07/T01）。gray.h / track_error.h 明示
 *    「不做赛道特征识别」所留的 Middleware 空白，由本模块唯一承接。
 * 3. 坐标与位序：position 按「车左(0)→车右(11)」（驾驶员视角，与 track_error 同一约定）。
 *    `bit0_is_left` 是位序左右的**唯一**修正点的透传应用——本模块与 track_error 一致地
 *    应用同一个标志，**不新增第二个反转开关**（encoder s_direction_sign 教训，§8.2）。
 * 4. 阈值 full_bar_min_count / branch_min_span 是几何安装事实（探头间距、线宽相关），
 *    由调用者在机械/H2 标定后给定，无默认值；本模块不猜几何。
 * 5. 置信计数以「连续 Update 拍数」计（非时间）：谓词某拍不成立即清 0，去除单拍毛刺。
 */
#ifndef HC_TEAM_MIDDLEWARE_TRACK_ELEMENTS_TRACK_ELEMENTS_H
#define HC_TEAM_MIDDLEWARE_TRACK_ELEMENTS_TRACK_ELEMENTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 阵列路数。Middleware 不含 Driver 头，自持常数；与 Driver/track_error 的一致性由调用者负责。 */
#define TRACK_ELEMENTS_CHANNEL_COUNT 12u

/**
 * 元素类别 = 单张位图可区分的几何形态。语义（十字 vs 终点、第几次）归调用者（S07/T01），此处不判。
 * 枚举值同时用作 enable_mask / confirmed_mask / 事件掩码的位号（bit = 1u << kind）。
 */
typedef enum {
    TRACK_ELEMENT_GAP = 0,      /* 断线：有效位全 0 */
    TRACK_ELEMENT_FULL_BAR,     /* 横线：触车左且触车右、深色路数 ≥ full_bar_min_count（十字或终点，不分语义） */
    TRACK_ELEMENT_BRANCH_LEFT,  /* 左岔 / 左直角：触车左、未触车右、深色跨度 ≥ branch_min_span */
    TRACK_ELEMENT_BRANCH_RIGHT, /* 右岔 / 右直角：触车右、未触车左、深色跨度 ≥ branch_min_span */
    TRACK_ELEMENT_COUNT
} TrackElement_Kind;

/** 检测配置。阈值是几何安装事实，由调用者提供。 */
typedef struct {
    bool     bit0_is_left;       /* bit0(=PIN_IN1) 是否位于车左（驾驶员视角）；位序唯一修正点 */
    uint8_t  full_bar_min_count; /* 判 FULL_BAR 的最小深色路数（1..12） */
    uint8_t  branch_min_span;    /* 判 BRANCH 的最小深色跨度（1..12） */
    uint8_t  confirm_ticks;      /* 连续满足多少拍置 confirmed（去毛刺）；0 归一化为 1 */
    uint16_t enable_mask;        /* bit(kind)=1 启用该检测器；未启用者永不计数 / 触发 */
} TrackElements_Config_T;

/**
 * 检测器上下文。**调用者分配**（栈或静态皆可，无 malloc）。
 * 全部字段为**私有状态**——调用者不得直接读写，一律经下方 API 访问。
 */
typedef struct {
    TrackElements_Config_T cfg;
    uint8_t  count[TRACK_ELEMENT_COUNT]; /* 各检测器当前连续置信计数，饱和于 confirm_ticks */
    uint16_t confirmed_mask;             /* 当前确认电平掩码（count≥confirm_ticks 的检测器集合） */
    uint16_t just_confirmed_mask;        /* 最近一拍新升起的确认（上升沿事件），PollEvents 取后清 */
} TrackElements_Detector_T;

/**
 * @brief  初始化 / 复位检测器：存配置，计数与掩码清零。
 * @param  det  上下文，调用者持有。det==NULL 或 cfg==NULL 时静默无副作用返回吸收
 *              （同 pid/odometry 口径：NULL 视为调用者误用，不做断言/错误码，也不改任何状态）。
 * @param  cfg  检测配置；confirm_ticks==0 归一化为 1。按值拷入 det->cfg。
 */
void TrackElements_Init(TrackElements_Detector_T *det, const TrackElements_Config_T *cfg);

/**
 * @brief  推进一拍：由深色位图更新各启用检测器的置信计数与确认状态。
 * @param  det          上下文，det==NULL 无副作用。
 * @param  dark_bitmap  深色位图，仅低 TRACK_ELEMENTS_CHANNEL_COUNT 位有效（高位内部屏蔽）；
 *                      bit=1 表示该路压在深色（黑线）上。
 * @note   谓词成立则该检测器 count 饱和自增至 confirm_ticks、否则清 0；count≥confirm_ticks 置
 *         confirmed；本拍由未确认转确认者进 just_confirmed_mask。未启用检测器不参与。
 */
void TrackElements_Update(TrackElements_Detector_T *det, uint16_t dark_bitmap);

/**
 * @brief  取出并清空元素确认的上升沿事件掩码（段切换触发源）。
 * @return bit(kind)=1 表示该元素在最近一拍由未确认转为确认；随即清零。det==NULL 返回 0。
 */
uint16_t TrackElements_PollEvents(TrackElements_Detector_T *det);

/**
 * @brief  当前确认电平掩码。谓词持续成立期间保持置位；不清事件。
 * @return bit(kind)=1 表示该元素当前处于确认态。det==NULL 返回 0。
 */
uint16_t TrackElements_GetConfirmed(const TrackElements_Detector_T *det);

/**
 * @brief  某检测器当前连续置信计数（硬件标定 confirm_ticks / 阈值用）。
 * @return 0..confirm_ticks。det==NULL 或 kind 越界返回 0。
 */
uint8_t TrackElements_GetConfidence(const TrackElements_Detector_T *det, TrackElement_Kind kind);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_MIDDLEWARE_TRACK_ELEMENTS_TRACK_ELEMENTS_H */
