/**
 * @file    track_follow.h
 * @brief   灰度循迹采样与误差计算模块对外接口定义
 *
 * 本模块对外提供 12 路灰度传感器位图采样、字符串格式化与误差计算接口。
 *
 * 功能范围：
 * - 读取灰度传感器输入并更新当前位图
 * - 对外导出低 TRACK_SENSOR_COUNT 位位图结果
 * - 提供位图字符串格式化
 * - 提供循迹 PID 所需的误差计算接口
 *
 * 设计约定：
 * - 仅使用低 TRACK_SENSOR_COUNT 位作为灰度输入有效位
 * - 误差范围按线性权重生成，并最终限幅
 */

#ifndef __TRACK_H
#define __TRACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

    /* ---- 常量与全局状态 ----------------------------------------------------- */

#define TRACK_SENSOR_COUNT     12u//当前使用的灰度传感器数量
#define TRACK_BITMAP_STR_LEN   13u//12 位二进制字符串 + 1 字节结束符
/* 位图有效位掩码:bit0=IN1(最左) .. bit11=IN12(最右) */
#define TRACK_BITMAP_MASK      ((1u << TRACK_SENSOR_COUNT) - 1u)

/* Lower TRACK_SENSOR_COUNT bits are valid: bit0=IN1, bit11=IN12. */
    extern uint32_t TrackN;

    /* ---- 公开 API ----------------------------------------------------------- */

    void Track_UpdateSample(void);//更新灰度传感器采样位图
    uint32_t Track_GetBitmap(void);//获取当前灰度位图，低 12 位有效
    void TrackN_To_BitmapString(char* buffer, uint32_t trackMap);//将灰度位图转换为字符串，输出格式为 12 位二进制字符串
    int16_t Calculate_Track_Error(uint32_t trackMap);//根据当前灰度位图计算循迹误差，误差范围按线性权重生成，并最终限幅在 [-55, 55]

#ifdef __cplusplus
}
#endif

#endif /* __TRACK_H */
