/**
 * @file    track_follow.c
 * @brief   灰度循迹采样与误差计算模块实现
 *
 * 本文件实现 8 路灰度传感器的采样、位图格式化与循迹误差计算。
 *
 * 功能范围：
 * - 采集 8 路灰度输入并更新位图
 * - 提供位图读取与字符串格式化
 * - 按线性权重计算循迹误差
 *
 * 不负责的内容：
 * - 电机控制输出
 * - PID 参数整定
 * - 调度与系统状态切换
 *
 * 实现说明：
 * 1. 仅使用低 8 位记录当前灰度输入状态
 * 2. 误差按线性权重生成，并对最终结果做限幅
 * 3. 丢线时按上次误差方向返回回退值
 */

#include <stdint.h>
#include <stddef.h>
#include "track_follow.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_gpio.h>

 /* ---- 模块状态 ----------------------------------------------------------- */

uint32_t TrackN = 0u;

/* bit0=IN1 .. bit7=IN8，与既有灰度位图顺序一致 */
static const uint32_t s_track_sensor_pins[TRACK_SENSOR_COUNT] = {
    GPIO_LINE_SENSOR_PIN_IN1_PIN,
    GPIO_LINE_SENSOR_PIN_IN2_PIN,
    GPIO_LINE_SENSOR_PIN_IN3_PIN,
    GPIO_LINE_SENSOR_PIN_IN4_PIN,
    GPIO_LINE_SENSOR_PIN_IN5_PIN,
    GPIO_LINE_SENSOR_PIN_IN6_PIN,
    GPIO_LINE_SENSOR_PIN_IN7_PIN,
    GPIO_LINE_SENSOR_PIN_IN8_PIN,
};

/* ---- 采样接口 ----------------------------------------------------------- */

/**
 * @brief 更新灰度传感器采样位图
 */
void Track_UpdateSample(void)
{
    uint32_t index = 0u;
    uint32_t track_map = 0u;

    for (index = 0u; index < TRACK_SENSOR_COUNT; index++) {
        /* Active-high bit set when pin reads non-zero (matches old HC_PIN_SET path). */
        if (DL_GPIO_readPins(GPIO_LINE_SENSOR_PORT,
                             s_track_sensor_pins[index]) != 0u) {
            track_map |= (1u << index);
        }
    }

    TrackN = track_map;/* 更新全局位图变量，低 8 位有效 */
}

/**
 * @brief  获取当前灰度位图
 * @return 低 8 位有效的灰度输入位图
 */
uint32_t Track_GetBitmap(void)
{
    return TrackN & 0xFFu;
}//返回当前灰度位图，确保仅低 8 位有效

/* ---- 位图格式化 --------------------------------------------------------- */

/**
 * @brief 将灰度位图转换为字符串
 * @param buffer  用于接收字符串的缓冲区
 * @param trackMap 当前灰度位图
 */
void TrackN_To_BitmapString(char* buffer, uint32_t trackMap)
{
    int32_t bit_index = 0;
    uint32_t write_index = 0u;

    if (buffer == NULL) {
        return;
    }

    for (bit_index = (int32_t)TRACK_SENSOR_COUNT - 1; bit_index >= 0; bit_index--) {
        buffer[write_index++] =
            ((trackMap & (1u << (uint32_t)bit_index)) != 0u) ? '1' : '0';
    }

    buffer[TRACK_SENSOR_COUNT] = '\0';
}

/* ---- 误差计算 ----------------------------------------------------------- */

static int16_t Track_GetLinearWeight(uint32_t index, int16_t max_error)
{
    /* 中文说明：
     * 1. 这里把 8 路灰度的权重按“等差线性变化”生成，而不是手写固定数组。
     * 2. 本次修改的是“实现方式”，不是“实际权重结果”。
     * 3. 当 TRACK_SENSOR_COUNT=8、max_error=55 时，最终数值仍然是：
     *    index 0~7 => -55, -39, -23, -7, 7, 23, 39, 55。
     * 4. 这样后续如果传感器路数或最大误差调整，权重会自动按平均步长变化。
     */
    const int32_t center_span = (int32_t)TRACK_SENSOR_COUNT - 1;
    const int32_t scaled_index =
        ((int32_t)index * 2) - center_span;

    return (int16_t)((scaled_index * (int32_t)max_error) / center_span);
}

int16_t Calculate_Track_Error(uint32_t trackMap)
{
    static int16_t last_error = 0;
    /* 中文说明：
     * 1. kMaxError 仍保持 55，不做数值修改。
     * 2. kFallbackError 仍按 55/2 计算，得到 27，同样未改动策略。
     * 3. 本次改动重点是把权重表改成线性公式，误差范围和限幅范围保持不变。
     */
    const int16_t kMaxError = 55;
    const int16_t kFallbackError = kMaxError / 2;
    int32_t error_sum = 0;
    uint32_t sensor_count = 0u;
    uint32_t index = 0u;

    /* 中文说明：
     * 这里只保留低 8 位灰度数据参与 PID 计算。
     * 本次没有改数值范围，仍然是 0x00~0xFF 这 8 位有效。
     */
    trackMap &= 0xFFu;

    if (trackMap == 0u) {
        /* 中文说明：
         * 1. 全部传感器未检测到有效线时，进入丢线回退逻辑。
         * 2. 回退数值仍然沿用原策略：按上次误差方向返回 +/-27。
         * 3. 这部分本次没有做数值修改，只补充了线性权重后的说明。
         */
        if (last_error > 0) {
            return kFallbackError;
        }
        if (last_error < 0) {
            return -kFallbackError;
        }
        return 0;
    }

    for (index = 0u; index < TRACK_SENSOR_COUNT; index++) {
        if ((trackMap & (1u << index)) != 0u) {
            /* 中文说明：
             * 1. 每触发一路灰度，就累加这一位对应的线性权重。
             * 2. 当前 8 路实际权重数值依次为：
             *    IN1~IN8 => -55, -39, -23, -7, 7, 23, 39, 55。
             * 3. 相比上一次版本，数值结果没有变化，只是从“固定数组”改成“平均变化公式”。
             */
            error_sum += Track_GetLinearWeight(index, kMaxError);
            sensor_count++;
        }
    }

    if (sensor_count == 0u) {
        return 0;
    }

    {
        /* 中文说明：
         * 1. 这里继续使用“触发通道权重平均值”作为当前循迹误差。
         * 2. 平均方式没有变化，仍然是 error_sum / sensor_count。
         * 3. 最终误差再限制在 [-55, 55]，限幅数值保持不变。
         */
        int16_t error = (int16_t)(error_sum / (int32_t)sensor_count);

        if (error < -kMaxError) {
            error = -kMaxError;
        }
        if (error > kMaxError) {
            error = kMaxError;
        }

        /* 中文说明：
         * last_error 继续保存本次有效误差，供下次丢线时输出 +/-27 使用。
         * 这部分逻辑和数值策略均未改动。
         */
        last_error = error;
        return error;
    }
}
