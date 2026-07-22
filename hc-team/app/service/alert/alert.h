/**
 * @file    alert.h
 * @brief   声光提示服务：语义节拍编排（能力：装置能发出可听/可见的提示）。
 *
 * 预测缺口 P1 落地（契约 §33）：单响/双响/长响=一次性提示（播完自灭），
 * 常亮/慢闪/快闪/声光同步=持续指示（直至 Stop 或换模式）。
 * 节拍/相位唯一在本服务；引脚电平唯一经 driver/beacon（§8.2 单一所有者）。
 */
#ifndef ALERT_H
#define ALERT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALERT_PATTERN_NONE = 0,
    ALERT_PATTERN_BEEP_SHORT,   /**< 单响 100ms（一次性，播完自灭）。 */
    ALERT_PATTERN_BEEP_DOUBLE,  /**< 双响 100on/100off/100on（一次性）。 */
    ALERT_PATTERN_BEEP_LONG,    /**< 长响 500ms（一次性）。 */
    ALERT_PATTERN_LED_ON,       /**< LED 常亮（持续，直至 Stop/换模式）。 */
    ALERT_PATTERN_BLINK_SLOW,   /**< LED 500ms 半周期闪（持续，运行中指示）。 */
    ALERT_PATTERN_BLINK_FAST,   /**< LED 100ms 半周期闪（持续，告警）。 */
    ALERT_PATTERN_BEEP_BLINK,   /**< 声光同步 250ms 半周期（持续，赛日确认）。 */
} Alert_Pattern_T;

/** @brief Beacon 全灭 + 状态复位；不发其他硬件命令。 */
void Alert_Init(void);

/**
 * @brief 立即从头起播指定模式（换模式即重置相位，无跨模式残留）。
 * @param pattern 目标模式；NONE 等效 Alert_Stop()；超出枚举范围按 NONE 处理。
 */
void Alert_Play(Alert_Pattern_T pattern);

/**
 * @brief 节拍推进。相位以首个 Update 的 now_ms 播种（同仓惯例）。
 * @param now_ms 调度器注入的当前毫秒时刻（无符号减法天然处理回绕）。
 */
void Alert_Update(uint32_t now_ms);

/** @brief 全灭 + 回 NONE。确定性静默。 */
void Alert_Stop(void);

/** @brief 一次性模式在播=true；持续模式恒 true 直至 Stop；NONE=false。 */
bool Alert_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif /* ALERT_H */
