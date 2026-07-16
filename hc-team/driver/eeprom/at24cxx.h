/*
 * @file    AT24CXX.h
 * @brief   AT24C02 EEPROM 驱动接口定义
 * @version 1.0.0
 * @date    2025-10-28
 *
 * 仅针对 AT24C02（2 Kbit）器件提供读写封装。
 * I2C 绑定私有到 SysConfig I2C_AUX_INST（与 OLED 同总线）。
 * 所有注释均采用 UTF-8 编码，便于跨平台阅读。
 */

#ifndef AT24CXX_H__
#define AT24CXX_H__

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "stdio.h"

/* Exported defines ----------------------------------------------------------*/
/* AT24CXX 各容量器件的地址上限（用于兼容其他型号时参考）。 */
#define AT24C01     (127)       /* AT24C01  容量: 128字节   (1Kbit) */
#define AT24C02     (255)       /* AT24C02  容量: 256字节   (2Kbit) */
#define AT24C04     (511)       /* AT24C04  容量: 512字节   (4Kbit) */
#define AT24C08     (1023)      /* AT24C08  容量: 1024字节  (8Kbit) */
#define AT24C16     (2047)      /* AT24C16  容量: 2048字节  (16Kbit) */
#define AT24C32     (4095)      /* AT24C32  容量: 4096字节  (32Kbit) */
#define AT24C64     (8191)      /* AT24C64  容量: 8192字节  (64Kbit) */
#define AT24C128    (16383)     /* AT24C128 容量: 16384字节 (128Kbit) */
#define AT24C256    (32767)     /* AT24C256 容量: 32768字节 (256Kbit) */

/* 当前驱动仅针对 AT24C02，可按需修改为其他型号。 */
#define E2PROM_TYPE                             (AT24C02)

/* AT24C02 容量与页大小。 */
#define AT24_TOTAL_SIZE                         ((uint16_t)256)
#define AT24_PAGE_SIZE                          ((uint8_t)8)

/* AT24C02 使用 8 位内存地址 */
#define AT24_MEM_ADDR_SIZE_BITS                 (8)

/*
 * AT24C02 7-bit I2C 地址。A2~A0 接地时为 0x50。
 * DriverLib DL_I2C_startControllerTransfer 期望 7-bit 地址；
 * 旧宏曾用 8-bit 写地址 0xA0，迁移时改为 0x50。
 */
#define AT24C_DEV_ADDR (0x50u)

/* Exported functions prototypes ---------------------------------------------*/
/**
 * @brief  初始化 AT24CXX
 * @note   该函数会自动检测 EEPROM 是否正常工作
 * @retval None
 */
void at24cxx_init(void);

/**
 * @brief  向 AT24CXX 指定地址写入数据
 * @param  WriteAddr: 写入数据的起始地址 (0 ~ EE_TYPE)
 * @param  pBuffer:   指向要写入数据的缓冲区指针
 * @param  NumToWrite: 要写入的字节数
 * @retval None
 */
void at24cxx_write(uint16_t _usWriteAddr, uint8_t *_ucpBuffer, uint16_t _usNumToWrite);

/**
 * @brief  从 AT24CXX 指定地址读取数据
 * @param  ReadAddr:  读取数据的起始地址 (0 ~ EE_TYPE)
 * @param  pBuffer:   指向存储读取数据的缓冲区指针
 * @param  NumToRead: 要读取的字节数
 * @retval None
 */
void at24cxx_read(uint16_t _usReadAddr, uint8_t *_ucpBuffer, uint16_t _usNumToRead);

#endif /* AT24CXX_H__ */
