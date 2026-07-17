/**
 * @file    hmi.h
 * @brief   人机输入/显示服务（App Service 层）——语义输入事件与行式文本显示的唯一上层接口面。
 *
 * 抽象（人机面板能做什么）：
 * - 报告用户输入动作（上/下/确认/返回四个语义键的单次按下事件）；
 * - 按行显示 ASCII 文本（4 行 × 16 列，16px 字模）与清屏；
 * - 报告显示是否就绪；
 * - 被周期推进（按键扫描节奏 + 显示初始化泵送）。
 *
 * 隐藏：
 * - 用了哪些 Driver、物理按键编号到语义动作的映射（本服务是唯一映射点）、
 *   扫描周期、显示页寻址与字模细节、就绪门控。
 *
 * 分层与所有权：
 * - 去抖/单次事件锁存归 key 驱动；页寻址/字模/总线恢复归 oled 驱动；
 *   边沿位图归 BoardGpio/GROUP1 ISR。本服务唯一拥有：语义映射 + 泵送节奏 + 行式显示语义。
 *
 * 调用前置条件：
 * - System 装配层已完成 Key_Init()、OLED_Init()（含底层 I2C/Clock 初始化）。
 */
#ifndef HMI_H
#define HMI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 语义输入动作（板载 K1..K4 的唯一映射点在本服务内部）。 */
typedef enum {
    HMI_INPUT_NONE = 0,
    HMI_INPUT_UP,      /* 板载 K1 */
    HMI_INPUT_DOWN,    /* 板载 K2 */
    HMI_INPUT_ENTER,   /* 板载 K3 */
    HMI_INPUT_BACK,    /* 板载 K4 */
} Hmi_Input;

#define HMI_DISPLAY_ROWS 4u    /* 64px / 16px 字模 */
#define HMI_DISPLAY_COLS 16u   /* 128px / 8px 字宽 */

/** 门控基准/私有状态复位；不触碰按键与显示硬件。 */
void Hmi_Init(void);

/**
 * @brief 周期推进：自门控 5ms（Clock_NowMs 无符号减法）。到期执行：
 *        显示未就绪则推进其非阻塞初始化；扫描按键。允许被更快调用。
 */
void Hmi_Update(void);

/** 取出一个待处理语义输入事件（读清）；无事件返回 HMI_INPUT_NONE。 */
Hmi_Input Hmi_PollInput(void);

/** 显示初始化是否完成。 */
bool Hmi_IsDisplayReady(void);

/**
 * @brief  整行显示文本：超长截断至 16 列，不足行尾空格填满（整行所有权，
 *         行级覆写无残影）。
 * @param  row   行号 0..3。
 * @param  text  ASCII 字符串。
 * @return true = 整行已绘制；false = 未就绪/row 越界/text 为 NULL（此三类
 *         拒绝路径零绘制事务），或运行期总线错误（此时行内容不确定——
 *         逐字符事务可能中途失败；行级覆写幂等，重试整行即可恢复）。
 */
bool Hmi_PrintLine(uint8_t row, const char *text);

/** 清屏。未就绪返回 false。 */
bool Hmi_ClearDisplay(void);

#ifdef __cplusplus
}
#endif

#endif /* HMI_H */
