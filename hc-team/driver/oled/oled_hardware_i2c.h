/**
 * @file    oled_hardware_i2c.h
 * @brief   OLED 硬件 I2C 驱动模块对外接口定义
 *
 * @details
 * 模块职责：
 * - 提供基于硬件 I2C 的 OLED 基础驱动
 * - 对上层暴露显示开关、清屏、字符/字符串/中文/位图绘制接口
 * - 管理 OLED 设备地址与总线恢复
 *
 * 设计约定：
 * 1. 本模块采用“全局实例 + Init 填充硬件绑定”的方式管理 OLED 硬件资源
 * 2. 字库数据由 oledfont.h 提供，本模块只负责页地址设置与字节发送
 * 3. 支持 6x8/8x16 ASCII 与 16x16 中文点阵显示
 * 4. 对外坐标遵循 SSD1306 页寻址：x 为列地址，y 为页地址
 * 5. I2C 绑定私有到 SysConfig I2C_AUX_INST（I2C1）
 *
 * 使用方式：
 * 1. 底层 I2C 与 SysTick 初始化完成后先调用 OLED_Init() 发起初始化
 * 2. 在主循环或周期任务中持续调用 OLED_Process()，直到 OLED_IsReady() 返回 true
 * 3. 再调用 OLED_Clear()/OLED_ShowString()/OLED_ShowChinese() 等显示接口
 *
 * 注意：
 * - 当前驱动默认使用 128x64 OLED，页数为 8
 * - 本模块不维护显存缓存，写入后立即发送到屏幕
 * - 若总线异常，会尝试执行一次 I2C 总线恢复后重发
 */

#ifndef __OLED_HARDWARE_I2C_H__
#define __OLED_HARDWARE_I2C_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_CMD   0u
#define OLED_DATA  1u

/* ---- 类型定义 ----------------------------------------------------------- */

/**
 * @brief OLED 操作状态
 * @note  0 表示成功；负值表示失败原因
 */
typedef enum {
    OLED_OK = 0,
    OLED_ERR_UNKNOWN = -1,
    OLED_ERR_INVALID = -2,
    OLED_ERR_NULL_PTR = -3,
    OLED_ERR_TIMEOUT = -12,
} Oled_Status_e;

/**
 * @brief OLED I2C 总线绑定信息
 * @note  仅描述设备地址；控制器实例由驱动私有绑定到 I2C_OLED
 */
typedef struct {
    uint8_t           dev_addr; /**< OLED 7bit 设备地址 */
} OLED_Bus_T;

/**
 * @brief OLED 设备实例
 * @note  当前仅维护总线绑定，便于后续扩展为多屏或多种初始化序列
 */
typedef struct {
    OLED_Bus_T bus;
} OLED_T;

/* ---- 全局实例 ----------------------------------------------------------- */

extern OLED_T g_tOLED;

/* ---- 公开 API ----------------------------------------------------------- */

/* 反色显示开关。enable_inverse=0 为正常显示，非 0 为反色显示。 */
void OLED_ColorTurn(uint8_t enable_inverse);

/* 旋转显示开关。enable_rotate=0 为常规方向，非 0 为镜像翻转方向。 */
void OLED_DisplayTurn(uint8_t enable_rotate);

/* 向 OLED 写入一个字节。mode=OLED_CMD 表示命令，mode=OLED_DATA 表示数据。 */
Oled_Status_e OLED_WR_Byte(uint8_t dat, uint8_t mode);

/* 设置当前页地址与列地址。 */
void OLED_Set_Pos(uint8_t x, uint8_t y);

/* 开启 OLED 显示输出。 */
void OLED_Display_On(void);

/* 关闭 OLED 显示输出。 */
void OLED_Display_Off(void);

/* 清空整个 OLED 显示区域。 */
Oled_Status_e OLED_Clear(void);

/* 显示单个 ASCII 字符，支持 8/16 高度字模。 */
Oled_Status_e OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t sizey);

/* 计算 m 的 n 次方，供数字显示接口做位权拆分。 */
uint32_t oled_pow(uint8_t m, uint8_t n);

/* 显示无符号十进制数字。 */
Oled_Status_e OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len,
                        uint8_t sizey);

/* 显示 ASCII 字符串。 */
Oled_Status_e OLED_ShowString(uint8_t x, uint8_t y, const char *chr,
                           uint8_t sizey);

/* 按字库索引显示一个中文字符，当前支持 16x16 点阵。 */
Oled_Status_e OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t no, uint8_t sizey);

/* 绘制位图数据。 */
void OLED_DrawBMP(uint8_t x, uint8_t y, uint8_t sizex, uint8_t sizey,
                  const uint8_t BMP[]);

/* 发起 OLED 非阻塞初始化流程。 */
void OLED_Init(void);

/* 推进 OLED 非阻塞初始化状态机。 */
Oled_Status_e OLED_Process(void);

/* 查询 OLED 是否已经完成初始化。 */
bool OLED_IsReady(void);

/* I2C 总线恢复，用于 OLED 通信异常后的解锁。 */
void oled_i2c_sda_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_HARDWARE_I2C_H__ */
