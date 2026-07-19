/**
 * @file    gray_check.h
 * @brief   12 路灰度数字量遥测诊断服务（App Service 层）——只读、只发 VOFA。
 *
 * 抽象（诊断能做什么）：
 * - 进入/退出一次 12 路灰度数字量遥测（进入即挂 VOFA tx×12 组、退出清组）；
 * - 被周期推进：自读 12 路 0/1（深色/浅色）并发上位机，供人逐路肉眼确认接线。
 *
 * 用途：VOFA 上一路一路看每路当前 0/1，核对哪路压到黑线、接线是否对位（现场再调）。
 *
 * 隐藏：
 * - 用了哪些 Driver、VOFA 变量组存储、发帧节奏、通道顺序。
 *
 * 分层与所有权（AGENTS.md §4/§8.2）：
 * - 12 路原子读唯一在 `driver/gray`（`Gray_ReadDarkBitmap` 一次 DL_GPIO 读全 12 路）——
 *   本服务只 `(bitmap>>i)&1` 单向复制到 12 个 tx，零反相/去抖/滤波/阈值/左右重排
 *   （加任一样都构成第二所有者）。器件侧已含比较器/迟滞/滤波（gray.h 器件事实）。
 * - VOFA 协议/解析/缓冲/串口归 uart_vofa / vofa_uart Driver。本服务唯一拥有：诊断 tx 组 + 发帧节奏。
 * - Gray_ReadDarkBitmap 读点：本服务是继 line_follow 之后第二个调用点。但 gray 是无状态原子读、
 *   无累计器，两读点即便同拍也无数据冒险（不同于 encoder 累计 double-count）；scheduler
 *   单活动条目不变量保证与 line_follow 永不同拍。
 *
 * ★ 位序：仅镜像原始通道序 —— ch0..ch11（上位机 G1..G12）= Gray bit0..bit11 = board.syscfg
 *   GPIO_LINE_SENSOR 组 PIN_IN1..PIN_IN12。本服务**不声明车上左右/上下**（gray.h 位序警告：
 *   厂商手册与板注释矛盾、固件无法自证）。哪路 G 对应车上哪个物理位置由使用者对照
 *   board.syscfg 引脚表现场人工确认；左右修正唯一点在上层循迹权重表，本诊断不碰。
 *
 * 调用前置条件（System 装配层负责）：
 * - 已完成 `vofa_init()`（含 VofaUart_Init）。灰度无 Init（IO 输入由 SysConfig 配好，无内部状态）。
 */
#ifndef GRAY_CHECK_H
#define GRAY_CHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 进入诊断：清 VOFA profile → 注册 tx×12（G1..G12 = bit0..bit11，无 cmd）→ 门控基准置 0。不发电机命令。 */
void GrayCheck_Start(void);

/**
 * @brief 周期推进：自门控 10ms（now_ms 无符号减法）。到期执行
 *        Gray_ReadDarkBitmap() → 逐位镜像 12 路 0/1 → vofa_run（发本拍刷新帧，无一帧延迟）。
 * @param now_ms System 装配层供给的毫秒时刻（经 scheduler on_step 注入）。
 *
 * @note 无播种拍：gray 是无状态原子读，无 elapsed 消费者（不同于 encoder_test 需 elapsed
 *       喂 Encoder_Update）。门控基准初值 0 使进页首拍即发一帧（即时反馈）。
 */
void GrayCheck_Update(uint32_t now_ms);

/** 退出诊断：清 VOFA profile。本服务从不驱动电机，无电机需停。 */
void GrayCheck_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* GRAY_CHECK_H */
