/**
 * @file    gray_test.h
 * @brief   DEBUG GRAY_TEST 调试服务对外接口定义
 *
 * 本模块对外提供 GRAY_TEST 运行项的初始化、运行态切换与采样遥测接口。
 *
 * 功能范围：
 * - 管理 12 路灰度数字量与位图缓存的输出链路
 * - 提供运行项 Enter/Exit 生命周期接口
 * - 提供 10ms 采样与遥测入口
 * - 不提供专用 OLED 渲染，运行页统一回落到默认 RUNNING
 *
 * 设计约定：
 * 1. 第一版只复用现有 GPIO 数字量灰度链路，不引入 ADC
 * 2. 所有串口输出统一复用 VOFA FireWater CSV
 * 3. 本模块不保留跨运行项的 VOFA profile，切入时总是重建
 */

#ifndef __GRAY_TEST_H__
#define __GRAY_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 公开 API ----------------------------------------------------------- */

/** 初始化 GRAY_TEST 模块本地缓存。 */
void GrayTest_Init(void);

/** 进入 GRAY_TEST 运行项并重建当前 VOFA profile。 */
void GrayTest_Enter(void);

/** 退出 GRAY_TEST 运行项并清空当前 VOFA profile。 */
void GrayTest_Exit(void);

/** GRAY_TEST 10ms 采样与遥测入口。 */
void GrayTest_SampleAndTelemetry10ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __GRAY_TEST_H__ */
