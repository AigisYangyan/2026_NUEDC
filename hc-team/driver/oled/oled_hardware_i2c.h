/**
 * @file    oled_hardware_i2c.h
 * @brief   OLED 硬件 I2C 显示驱动公共接口
 *
 * @details
 * 对上层仅暴露显示能力：初始化、清屏、显示字符/字符串、初始化推进、
 * 就绪查询。总线实例、SSD1306 页寻址、字模表、总线恢复与等待上限均为
 * 驱动私有实现细节。
 */

#ifndef __OLED_HARDWARE_I2C_H__
#define __OLED_HARDWARE_I2C_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OLED 操作状态
 * @note  0 表示成功；负值表示失败原因。
 */
typedef enum {
    OLED_OK = 0,
    OLED_ERR_UNKNOWN = -1,
    OLED_ERR_INVALID = -2,
    OLED_ERR_NULL_PTR = -3,
    OLED_ERR_TIMEOUT = -12,
} Oled_Status_e;

/**
 * @brief 发起 OLED 非阻塞初始化流程
 * @note  底层 I2C 与时钟驱动完成初始化后调用。
 */
void OLED_Init(void);

/**
 * @brief 清空整个 OLED 显示区域
 * @return `OLED_OK` 表示成功，其他值表示总线或参数错误。
 */
Oled_Status_e OLED_Clear(void);

/**
 * @brief 显示单个 ASCII 字符
 * @param x      列地址，范围 `0..127`
 * @param y      页地址，范围 `0..7`
 * @param chr    ASCII 字符
 * @param sizey  字模高度，仅支持 `8` 或 `16`
 * @return `OLED_OK` 表示成功，越界/不支持字符返回 `OLED_ERR_INVALID`
 */
Oled_Status_e OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t sizey);

/**
 * @brief 显示 ASCII 字符串
 * @param x      起始列地址
 * @param y      起始页地址
 * @param chr    以 `\0` 结尾的字符串
 * @param sizey  字模高度，仅支持 `8` 或 `16`
 * @return `OLED_OK` 表示成功，其他值表示参数或总线错误。
 */
Oled_Status_e OLED_ShowString(uint8_t x, uint8_t y, const char *chr,
                              uint8_t sizey);

/**
 * @brief 推进 OLED 非阻塞初始化状态机
 * @note  `OLED_IsReady()` 仅表示初始化序列是否完成；运行期显示事务失败
 *       通过当前接口返回值上报，不会自动清空 ready 状态。
 * @return `OLED_OK` 表示当前步骤成功或仍在等待；其他值表示初始化事务失败。
 */
Oled_Status_e OLED_Process(void);

/**
 * @brief 查询 OLED 是否已经完成初始化
 * @return `true` 表示初始化序列完成，`false` 表示尚未完成。
 */
bool OLED_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_HARDWARE_I2C_H__ */
