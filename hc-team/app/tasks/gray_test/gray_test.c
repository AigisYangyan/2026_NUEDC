/**
 * @file    gray_test.c
 * @brief   DEBUG GRAY_TEST 调试服务实现
 *
 * 本文件实现 GRAY_TEST 运行项的灰度数字量采样与 VOFA 输出。
 *
 * 功能范围：
 * - 读取现有 12 路灰度 GPIO 数字量状态
 * - 维护 G1~G12 与位图缓存，供 VOFA 输出使用
 * - 在进入运行项时重建 GRAY_TEST 专属 VOFA profile
 * - 不提供专用 OLED 显示，运行页统一显示默认 RUNNING
 *
 * 不负责的内容：
 * - ADC 模拟灰度采样
 * - 循迹控制决策或 PID 闭环
 *
 * 实现说明：
 * 1. 输出 12 路 0/1 数字量与 1 个位图值
 * 2. 采样与遥测合并在同一个 10ms 周期入口中执行
 * 3. Enter/Exit 期间会切换当前 VOFA profile，避免残留其他 DEBUG 子任务通道
 */

#include "app/tasks/gray_test/gray_test.h"

#include "app/scheduler/vofa_register.h"
#include "app/tasks/track_follow/track_follow.h"
#include "driver/uart_vofa/uart_vofa.h"

/* ---- 静态辅助函数 ------------------------------------------------------- */

static void gray_test_refresh_cache(void)
{
    uint32_t bitmap = Track_GetBitmap();//获取当前灰度位图值
    uint32_t index = 0u;//索引变量
    VofaGrayTestCtx_t* ctx = VofaRegister_GetGrayTestCtx();

    for (index = 0u; index < TRACK_SENSOR_COUNT; index++) {
        ctx->tx_channels[index] = ((bitmap & (1u << index)) != 0u) ? 1 : 0;
    }//更新 G1~G12 数字量缓存，1 表示对应位图位为 1，0 表示对应位图位为 0

    ctx->tx_bitmap = (int)bitmap;//更新位图缓存
}

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief GRAY_TEST 模块初始化
 * @note  仅清零本地缓存，不注册运行态 profile
 */
void GrayTest_Init(void)
{
    VofaGrayTestCtx_t* ctx = VofaRegister_GetGrayTestCtx();
    uint32_t index = 0u;

    for (index = 0u; index < TRACK_SENSOR_COUNT; index++) {
        ctx->tx_channels[index] = 0;
    }
    ctx->tx_bitmap = 0;
}

/**
 * @brief 进入 GRAY_TEST 运行项
 * @note  立即采样一次并重建当前 VOFA profile
 */
void GrayTest_Enter(void)
{
    Track_UpdateSample();
    VofaRegister_EnterProfile(VOFA_PROFILE_GRAY_TEST);
    gray_test_refresh_cache();
}

/**
 * @brief 退出 GRAY_TEST 运行项
 */
void GrayTest_Exit(void)
{
    VofaRegister_ExitProfile();
}

/**
 * @brief GRAY_TEST 10ms 采样与遥测入口
 */
void GrayTest_SampleAndTelemetry10ms(void)
{
    Track_UpdateSample();
    gray_test_refresh_cache();
    vofa_run();
}
