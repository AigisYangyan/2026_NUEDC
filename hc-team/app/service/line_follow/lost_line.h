/**
 * @file    lost_line.h
 * @brief   丢线恢复策略（line_follow 服务内私有子模块）。
 *
 * 抽象：
 * - 记住最近一次有效误差的方向；丢线期间输出固定幅值的回退误差，
 *   使车沿最后见线的一侧继续转向找线；累计丢线时长有界，超时放弃。
 *
 * 语义来源：重建旧 track_follow 的 ±27 记忆回退（phase3 §5.2 移交备忘——
 * 旧语义刻意未迁移，此处为显式重建版：单位改 mm、超时上限新增，
 * 幅值/超时均由调用者配置，本模块不写死数值）。
 *
 * 所有权：上下文由调用者持有（同 Pid_T 模式），本模块无模块级状态。
 */
#ifndef LOST_LINE_H
#define LOST_LINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 丢线恢复上下文。字段仅供本模块读写（约定私有）。 */
typedef struct {
    float    recovery_error_mm;  /* 回退误差幅值 */
    uint32_t timeout_ms;         /* 累计丢线上限 */
    float    last_valid_error_mm;
    uint32_t lost_elapsed_ms;
} LostLine_T;

/** 初始化/复位策略：清方向记忆与计时，写入配置。 */
void LostLine_Init(LostLine_T *ctx, float recovery_error_mm, uint32_t timeout_ms);

/** 有线一拍：记录误差方向，丢线计时清零。 */
void LostLine_NoteValid(LostLine_T *ctx, float error_mm);

/**
 * @brief  丢线一拍：累计丢线时长并给出回退误差。
 * @param  elapsed_ms    距上一拍的真实毫秒数。
 * @param  out_error_mm  回退误差 = sign(最近有效误差) × 幅值；从未见线或
 *                       最后误差为 0 时输出 0（直行找线）。仅在返回 true 时写入。
 * @return true = 仍在恢复期；false = 累计达到 timeout_ms，放弃（调用方应停车）。
 */
bool LostLine_Tick(LostLine_T *ctx, uint32_t elapsed_ms, float *out_error_mm);

#ifdef __cplusplus
}
#endif

#endif /* LOST_LINE_H */
