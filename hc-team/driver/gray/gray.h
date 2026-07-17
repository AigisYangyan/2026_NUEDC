/**
 * @file    gray.h
 * @brief   12 路灰度传感器阵列 Driver 对外接口（武汉无名创新 NCHD1「迹」）
 *
 * 模块职责：
 * - 一次端口读取，产出 12 路灰度传感器的「深色」位图
 *
 * 依赖方向：
 * - Gray -> GrayPort -> DL HAL。本文件与 gray.c 不含任何 TI 头依赖。
 *
 * 器件事实（厂商手册《灰度传感器用户使用手册（迹系列20230119)》）：
 * - 模组出厂即数字输出（DEF 处焊有零欧电阻 R6，等效短接 OUT-IO）
 * - **深色背景 -> 输出高电平**；浅色背景 -> 低电平。这是比较器电路的直接结果，
 *   不是本 Driver 的约定，故接口按「dark」命名而非「line」或「black」
 * - 器件侧已有 C2(100pF) 滤波与电位器给定的比较迟滞裕度
 * - 纯电平输出：无时序、无握手、无错误码，故本 Driver 无诊断接口
 *
 * 本 Driver **刻意不做**的事（每一条都有单一所有者理由）：
 * - 不做初始化：器件要求的唯一初始化动作是「IO 配成输入」，SysConfig 已完成；
 *   本模块无内部状态，故无 Gray_Init()
 * - 不做去抖 / 滤波 / 反相 / 阈值：器件侧已做（见上），固件再做一次会形成第二个
 *   所有者（AGENTS.md §8.2）
 * - 不做「丢线」判断：位图全 0 是否等于丢线，是循迹算法对位图的解读，不是器件事实
 * - 不做误差量化：厂商示例的 -11..+11 权重映射属循迹算法，归 Middleware（§3.3）
 * - 不做赛道特征识别（起始线 / 十字 / 直角）：同上
 * - 不声明左右：见下方位序警告
 *
 * ★ 位序警告 —— 未实测前不得假设左右：
 *   厂商手册 p.34 约定 P1 = 阵列**最右**端；而本仓 board.syscfg 注释写
 *   「物理排列从左到右 IN1 -> IN12」。二者矛盾，且哪个物理位置接到 PIN_IN1(PB27)
 *   是硬件接线事实，固件无法自证。因此本接口只声明「bit i 对应 PIN_IN(i+1) 引脚」，
 *   不声明该引脚在车上的左右位置。
 *   左右方向的修正点只有一个，且必须落在上层的权重表里 —— 不得在本 Driver 里加
 *   第二个反转开关（同 encoder.c 的 s_direction_sign[] 教训，§8.2）。
 */
#ifndef HC_TEAM_DRIVER_GRAY_GRAY_H
#define HC_TEAM_DRIVER_GRAY_GRAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 阵列路数。与 board.syscfg 的 GPIO_LINE_SENSOR 组引脚数一致。 */
#define GRAY_CHANNEL_COUNT 12u

/** 位图有效位掩码：仅低 GRAY_CHANNEL_COUNT 位有效。 */
#define GRAY_BITMAP_MASK ((uint16_t)((1u << GRAY_CHANNEL_COUNT) - 1u))

/**
 * @brief  读取 12 路灰度传感器的当前深色位图。
 *
 * bit i = 1 表示 board.syscfg 中 GPIO_LINE_SENSOR 组的 PIN_IN(i+1) 引脚当前为
 * 高电平，即该路传感器下方是**深色**背景（黑线）。bit i = 0 表示浅色背景。
 * 高 4 位恒为 0。
 *
 * 12 路在硬件上全部落在同一端口（PORTB），本函数用**一次**端口读取取回全部 12 路，
 * 因此路间无时间偏斜。这正是 board.syscfg 特意让 IN4 占 PB8 而非跨端口的 PA7 所
 * 换来的性质；调用方可依赖同一次返回值内的 12 位来自同一时刻。
 *
 * @return 低 GRAY_CHANNEL_COUNT 位有效的深色位图。
 */
uint16_t Gray_ReadDarkBitmap(void);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_DRIVER_GRAY_GRAY_H */
