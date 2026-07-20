/**
 * @file    gray_check.h
 * @brief   12 路灰度标定助手服务（App Service 层）——只读诊断：VOFA 遥测 + OLED 标定面板。
 *
 * 抽象（标定助手能做什么）：
 * - 进入/退出一次 12 路灰度诊断（进入即挂 VOFA tx×12 组 + 清零标定统计，退出清组）；
 * - 被周期推进：自读 12 路 0/1（深色/浅色）发上位机，并在 OLED 实时显示
 *   「实时位图 / 进入以来粘滞深色 / 逐路跳变计数 / 位图十六进制+深色路数」四行面板。
 *
 * 用途（器件事实：NCHD1 阈值 = 板上电位器，固件无阈值可调——手册 p.16/p.23，全手册无
 * 软件阈值/一键标定途径）：现场光线变化后按厂商三步法重调电位器时，本面板把标定终判
 * 「上下微抖灯仍不变」变成可量化读数（白底扫察 S 行应全空、静置 T 行应不增长），
 * 且显示的是 MCU 实际读到的电平（含接线/串阻链路）——板上 LED 只反映模组侧。
 * 统计清零手势 = 重进条目（BACK→ENTER）。有 PC 时 VOFA 面照旧可用。
 *
 * 隐藏：
 * - 用了哪些 Driver/同层 Service、VOFA 变量组存储、发帧节奏、通道顺序、
 *   标定统计（粘滞/跳变计数/播种态）、面板行格式与重绘节奏（全私有，零 getter）。
 *
 * 分层与所有权（AGENTS.md §4/§8.2）：
 * - 12 路原子读唯一在 `driver/gray`（`Gray_ReadDarkBitmap` 一次 DL_GPIO 读全 12 路）——
 *   本服务只 `(bitmap>>i)&1` 单向复制到 12 个 tx，零反相/去抖/滤波/阈值/左右重排
 *   （加任一样都构成第二所有者）。器件侧已含比较器/迟滞/滤波（gray.h 器件事实）。
 * - 黑白判定唯一所有者 = 硬件电位器（V-com 比较电压）；本服务的粘滞/跳变统计只累计
 *   观察值，不回写数据链，不构成对位图的第二处理。
 * - VOFA 协议/解析/缓冲/串口归 uart_vofa / vofa_uart Driver；OLED 行写语义/就绪门控归
 *   hmi（Service→Service 同层受控，只用显示面 Hmi_PrintLine，不碰 Hmi_PollInput——
 *   语义输入唯一消费者是 menu）。本服务唯一拥有：诊断 tx 组 + 发帧节奏 + 标定统计 +
 *   面板格式化/重绘节奏。
 * - RUN_ACTIVE 整屏显示权经 menu self-draw opt-in 让渡本条目（W7 §29 修订 2）；
 *   进入后首个周期覆盖全部 4 行是本服务的交接义务。
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

/** 进入诊断：清 VOFA profile → 注册 tx×12（G1..G12 = bit0..bit11，无 cmd）→ 门控基准置 0
 *  → 标定统计清零 + 面板行缓存失效（首绘覆盖全 4 行）。不发电机命令。 */
void GrayCheck_Start(void);

/**
 * @brief 周期推进：自门控 10ms（now_ms 无符号减法）。到期执行同一次
 *        Gray_ReadDarkBitmap() → ① 逐位镜像 12 路 0/1 → vofa_run（发本拍刷新帧）；
 *        ② 标定统计累计（粘滞 OR / 逐路跳变，首拍只播种不计）。另按 100ms 面板门控
 *        行差分重绘 OLED（PrintLine 失败该行下周期重试）。
 * @param now_ms System 装配层供给的毫秒时刻（经 scheduler on_step 注入）。
 *
 * @note 门控基准无播种拍：gray 是无状态原子读，无 elapsed 消费者（不同于 encoder_test 需
 *       elapsed 喂 Encoder_Update）。基准初值 0 使进页首拍即发一帧+首绘面板（即时反馈）。
 *       跳变统计的 prev 播种是统计正确性所需（进条目压线路不算跳变），非门控播种拍回退。
 */
void GrayCheck_Update(uint32_t now_ms);

/** 退出诊断：清 VOFA profile；不清 OLED（BACK 后 menu 立即重绘子列表覆盖，无残影窗口）。
 *  本服务从不驱动电机，无电机需停。 */
void GrayCheck_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* GRAY_CHECK_H */
